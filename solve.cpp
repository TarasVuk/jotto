#include <iostream>
#include <iomanip>   
#include <fstream>
#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <immintrin.h>
#include <chrono>

#define charset uint32_t
#define microS uint64_t
#define hashV uint64_t

#define WORDLENGTH 5
#define SOLUTIONWORDS 5
#define TXTWORDS 12974

#define BUFFERSIZE TXTWORDS * (WORDLENGTH + 1)

#define BUCKETCHARS 3
#define BUCKETS 1 << BUCKETCHARS

#define HASHSIZE 4782969	// 3^14

#define MAXTHREADS std::thread::hardware_concurrency() - 1

#define output solutions
#define STATISTICS true

std::ofstream solutions("solutions.txt");

char char_buffer[BUFFERSIZE];

const std::string alphabet = "qxjzvfwbkgpmhdcytlnuroisea";
charset charmask[256] = { 0 };

int all_words = 0;
int lenght_words[256][BUCKETS] = { 0 };
std::unordered_map<charset, std::vector<int>> charset2buffer_index;
std::vector<charset> charsets[256][BUCKETS];

struct path
{
	charset charsets[SOLUTIONWORDS] = { 0 };
};
int solutions_count = 0;
std::mutex solutions_mutex;
std::vector<path> path_solutions;

uint64_t solve_calls = 0;

struct solve_params
{
	charset usedchars;
	bool free_char;
	int free_words;
	path current_path;
};
std::mutex queue_mutex;
std::vector<solve_params> queue;
std::vector<std::thread> threads;

charset failed_usedchars[HASHSIZE] = { 0 };

hashV gethash(charset set)
{
	std::hash<charset> h;
	return h(set) % HASHSIZE;
}

inline uint32_t blsr(uint32_t x)
{
	return x & (x - 1);
}

void setcharmask()
{
	// Sets charmask alphabetically in reverse frequency order
	for (int x = 0; x < alphabet.size(); x++)
		charmask[alphabet[x]] = 1 << x;
}

void readwords(std::string file_name)
{
	std::ifstream words_alpha(file_name);

	// Writes words to char_buffer
	words_alpha.read(char_buffer, BUFFERSIZE);

	// Read words
	for (int buffer_index = 0; buffer_index < BUFFERSIZE; buffer_index += WORDLENGTH + 1)
	{
		charset set = 0;

		for (int x = 0; x < WORDLENGTH; x++)
			set |= charmask[char_buffer[buffer_index + x]];

		// If a word does not have all the different letters, it is ignored
		if (__builtin_popcount(set) != WORDLENGTH)
			continue;

		charset2buffer_index[set].emplace_back(buffer_index);	// Adds buffer_index to charset2buffer_index

		// Filters anagrams
		if (charset2buffer_index[set].size() > 1)
			continue;

		charset lowest_bit = set ^ blsr(set);	// Lowest unset bit
		char lowest_char = alphabet[__builtin_ctz(lowest_bit)];	// Lowest unset char

		charset bucket = set >> (26 - BUCKETCHARS);

		all_words++;

		// Adds charset based on the rarest letter and using of the most common letters
		lenght_words[lowest_char][bucket]++;
		charsets[lowest_char][bucket].emplace_back(set);
	}
}

bool solve1(charset usedchars, bool free_char, path current_path)
{
	solve_calls++;

	bool succeeds = false;	// Any solution from here

	charset lowest_bit = usedchars ^ (~blsr(~usedchars));	// Lowest unset bit
	char lowest_char = alphabet[__builtin_ctz(lowest_bit)];	// Lowest unset char

	// Tries the selected words
	for (int bucket = 0; bucket < BUCKETS; bucket++)
		if (((bucket << (26 - BUCKETCHARS)) & usedchars) == 0)
			for (int index = 0; index < lenght_words[lowest_char][bucket]; index++)
				if ((charsets[lowest_char][bucket][index] & usedchars) == 0)
				{
					succeeds = true;

					current_path.charsets[0] = charsets[lowest_char][bucket][index];
					solutions_mutex.lock();
					path_solutions.emplace_back(current_path);
					solutions_mutex.unlock();
					current_path.charsets[0] = 0;
				}

	// Tries to leave the least common letter
	if (free_char)
		succeeds |= solve1(usedchars | lowest_bit, false, current_path);

	return succeeds;
}

bool solve(charset usedchars, bool free_char, int free_words, path current_path)
{
	solve_calls++;

	hashV hashv = gethash(usedchars);

	// Lookups failed_usedchars
	if (failed_usedchars[hashv] == usedchars)
		if (usedchars)
			return false;

	// For free_words less than or equal to 1, it calls solve1()
	if (free_words <= 1)
	{
		bool succeeds1 = solve1(usedchars, free_char, current_path);

		// Updates failed_usedchars and returns value
		if (!succeeds1)
		{
			if (__builtin_popcount(failed_usedchars[hashv]) < __builtin_popcount(usedchars))	// Depth-preferred replacement strategy
				failed_usedchars[hashv] = usedchars;
			return false;
		}
		else
			return true;
	}

	bool succeeds = false;	// Any solution from here

	charset lowest_bit = usedchars ^ (~blsr(~usedchars));	// Lowest unset bit
	char lowest_char = alphabet[__builtin_ctz (lowest_bit)];	// Lowest unset char

	// Tries the selected words
	for (int bucket = 0; bucket < BUCKETS; bucket++)
		if (((bucket << (26 - BUCKETCHARS)) & usedchars) == 0)
			for (int index = 0; index < lenght_words[lowest_char][bucket]; index++)
				if ((charsets[lowest_char][bucket][index] & usedchars) == 0)
				{
					current_path.charsets[free_words - 1] = charsets[lowest_char][bucket][index];
					succeeds |= solve(usedchars | charsets[lowest_char][bucket][index], free_char, free_words - 1, current_path);
					current_path.charsets[free_words - 1] = 0;
				}

	// Tries to leave the least common letter
	if (free_char)
		succeeds |= solve(usedchars | lowest_bit, false, free_words, current_path);

	// Updates failed_usedchars and returns value
	if (!succeeds)
	{
		if (__builtin_popcount(failed_usedchars[hashv]) < __builtin_popcount(usedchars))	// Depth-preferred replacement strategy
			failed_usedchars[hashv] = usedchars;
		return false;
	}
	else
		return true;
}

void threadloop()
{
	while (true)
	{
		queue_mutex.lock();
		if (queue.empty())
		{
			queue_mutex.unlock();
			return;
		}
		solve_params params = queue[queue.size() - 1];
		queue.pop_back();
		queue_mutex.unlock();

		solve(params.usedchars, params.free_char, params.free_words, params.current_path);
	}
}

void hypersolve(charset usedchars, bool free_char, int free_words, path current_path)
{
	solve_calls++;

	// For free_words less than or equal to 1, it calls solve1()
	if (free_words <= 1)
		solve1(usedchars, free_char, current_path);

	charset lowest_bit = usedchars ^ (~blsr(~usedchars));	// Lowest unset bit
	char lowest_char = alphabet[__builtin_ctz(lowest_bit)];	// Lowest unused char

	// Adds function parameters of selected words to queue
	for (int bucket = 0; bucket < BUCKETS; bucket++)
		if (((bucket << (26 - BUCKETCHARS)) & usedchars) == 0)
			for (int index = 0; index < lenght_words[lowest_char][bucket]; index++)
				if ((charsets[lowest_char][bucket][index] & usedchars) == 0)
				{
					current_path.charsets[free_words - 1] = charsets[lowest_char][bucket][index];
					queue.emplace_back(solve_params{ usedchars | charsets[lowest_char][bucket][index], free_char, free_words - 1, current_path });
					current_path.charsets[free_words - 1] = 0;
				}

	// Tries to leave the least common letter
	if (free_char)
	{
		solve_calls++;

		usedchars |= lowest_bit;

		charset next_lowest_bit = usedchars ^ (~blsr(~usedchars));	// Next lowest unset bit
		char next_lowest_char = alphabet[__builtin_ctz(next_lowest_bit)];	// Next lowest unused char

		// Adds function parameters of selected words to queue
		for (int bucket = 0; bucket < BUCKETS; bucket++)
			if (((bucket << (26 - BUCKETCHARS)) & usedchars) == 0)
				for (int index = 0; index < lenght_words[next_lowest_char][bucket]; index++)
					if ((charsets[next_lowest_char][bucket][index] & usedchars) == 0)
					{
						current_path.charsets[free_words - 1] = charsets[next_lowest_char][bucket][index];
						queue.emplace_back(solve_params{ usedchars | charsets[next_lowest_char][bucket][index], false, free_words - 1, current_path });
						current_path.charsets[free_words - 1] = 0;
					}
	}

	// Starts threads
	for (int x = 0; x < MAXTHREADS; x++)
		threads.emplace_back(std::thread(threadloop));

	// Stops threads
	for (int index = MAXTHREADS - 1; index >= 0; index--)
	{
		if (threads[index].joinable())
			threads[index].join();
		threads.pop_back();
	}
}

void savesolutions()
{
	for (int x = 0; x < path_solutions.size(); x++)
		for (int index1 = 0; index1 < charset2buffer_index[path_solutions[x].charsets[4]].size(); index1++)
			for (int index2 = 0; index2 < charset2buffer_index[path_solutions[x].charsets[3]].size(); index2++)
				for (int index3 = 0; index3 < charset2buffer_index[path_solutions[x].charsets[2]].size(); index3++)
					for (int index4 = 0; index4 < charset2buffer_index[path_solutions[x].charsets[1]].size(); index4++)
						for (int index5 = 0; index5 < charset2buffer_index[path_solutions[x].charsets[0]].size(); index5++)
						{
							solutions_count++;

							// Buffer_index-es of words 
							int word1 = charset2buffer_index[path_solutions[x].charsets[4]][index1];
							int word2 = charset2buffer_index[path_solutions[x].charsets[3]][index2];
							int word3 = charset2buffer_index[path_solutions[x].charsets[2]][index3];
							int word4 = charset2buffer_index[path_solutions[x].charsets[1]][index4];
							int word5 = charset2buffer_index[path_solutions[x].charsets[0]][index5];

							output << char_buffer[word1] << char_buffer[word1 + 1] << char_buffer[word1 + 2] << char_buffer[word1 + 3] << char_buffer[word1 + 4] << " "
								<< char_buffer[word2] << char_buffer[word2 + 1] << char_buffer[word2 + 2] << char_buffer[word2 + 3] << char_buffer[word2 + 4] << " "
								<< char_buffer[word3] << char_buffer[word3 + 1] << char_buffer[word3 + 2] << char_buffer[word3 + 3] << char_buffer[word3 + 4] << " "
								<< char_buffer[word4] << char_buffer[word4 + 1] << char_buffer[word4 + 2] << char_buffer[word4 + 3] << char_buffer[word4 + 4] << " "
								<< char_buffer[word5] << char_buffer[word5 + 1] << char_buffer[word5 + 2] << char_buffer[word5 + 3] << char_buffer[word5 + 4] << std::endl;
						}
}

int main()
{
	microS start_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

	// Reads words
	setcharmask();
	readwords("words_alpha.txt");

	microS read_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

	// Processes words
	path startpath;
	hypersolve(0, true, SOLUTIONWORDS, startpath);

	microS process_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

	// Writes solutions 
	savesolutions();

	microS end_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

	// Prints search statistics
#if STATISTICS == true
	std::cout << solutions_count << " solutions written to 'solutions.txt'." << std::endl;
	std::cout << "Read:       " << std::setw(6) << read_time - start_time << " \xE6m" << std::endl;
	std::cout << "Process:    " << std::setw(6) << process_time - read_time << " \xE6m" << std::endl;
	std::cout << "Write:      " << std::setw(6) << end_time - process_time << " \xE6m" << std::endl;
	std::cout << "Total time: " << std::setw(6) << end_time - start_time << " \xE6m" << std::endl;
	std::cout << std::endl;
	std::cout << "Used words: " << all_words << "/" << TXTWORDS << std::endl;
	std::cout << "Threads: " << MAXTHREADS << std::endl;
	std::cout << "solve() calls: " << solve_calls << std::endl;
#endif

	return 0;
}
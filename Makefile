CXX = g++
CXXFLAGS = -O3 -std=c++17 -march=native

all: clean
	$(CXX) $(CXXFLAGS) -fprofile-generate -fprofile-correction solve.cpp -o jotto.exe
	for i in {1..4}; do ./jotto; done
	$(CXX) $(CXXFLAGS) -fprofile-use solve.cpp -o jotto.exe

clean:
	rm -f jotto.exe *.gcda *.gcno

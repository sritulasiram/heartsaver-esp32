CXX ?= g++
test: test_logic.cpp ../lib/tracker_core/alert.h ../lib/tracker_core/debounce.h ../lib/tracker_core/heart_rate.h ../lib/tracker_core/at_modem.h
	$(CXX) -std=c++17 -Wall -Wextra -I../lib/tracker_core -o test_logic test_logic.cpp
	./test_logic
clean:
	rm -f test_logic
.PHONY: test clean

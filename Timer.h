#include <chrono>

class Timer {
public:
	Timer() {
		startPoint = std::chrono::high_resolution_clock::now();
	}
	// Not useful rn.
	//~Timer() {
	//	Stop();
	//}
	void Reset() {
		startPoint = std::chrono::high_resolution_clock::now();
	}
	double GetMilliseconds() {
		return Stop() * 0.001;
	}
	double GetMicroseconds() {
		return Stop();
	}
private:
	double Stop() {
		const auto end   = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()).time_since_epoch().count();
		const auto start = std::chrono::time_point_cast<std::chrono::microseconds>(startPoint).time_since_epoch().count();
		return end - start;
	}

	std::chrono::time_point<std::chrono::high_resolution_clock> startPoint;
};
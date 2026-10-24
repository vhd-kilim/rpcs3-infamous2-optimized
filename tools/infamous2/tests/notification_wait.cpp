// Standalone C++20 regression test for the shared completion wait helper.
// cl /std:c++20 /EHsc /O2 notification_wait.cpp
#include "../../../rpcs3/Emu/RSX/Common/notification_wait.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
void check(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

int main()
{
	std::atomic<int> count{0};
	unsigned pauses = 0;
	rsx::detail::wait_for_zero_notification(count, [&] { ++pauses; });
	check(pauses == 0, "already complete must return immediately");

	count = 1;
	rsx::detail::wait_for_zero_notification(count, [&]
	{
		if (++pauses == 3) count = 0;
	});
	check(pauses == 3, "short handoff should complete during bounded spin");

	struct completion_before_park
	{
		mutable std::atomic<int> value{1};
		int load() const { return value.load(); }
		void wait(int observed) const
		{
			// Force the last completion into the load-to-wait race window.
			value = 0;
			value.notify_all();
			value.wait(observed);
		}
	} race;
	rsx::detail::wait_for_zero_notification(race, [] {});
	check(race.load() == 0, "notification before parking must not be lost");

	for (int round = 0; round < 200; ++round)
	{
		constexpr int producers = 8;
		std::atomic<int> remaining{producers};
		std::atomic<bool> go{false};
		std::vector<int> results(producers);
		std::vector<std::thread> workers;
		for (int i = 0; i < producers; ++i)
		{
			workers.emplace_back([&, i]
			{
				go.wait(false);
				if (i == producers - 1) std::this_thread::sleep_for(1ms);
				results[i] = i + 1;
				if (remaining.fetch_sub(1) == 1) remaining.notify_all();
			});
		}
		go = true;
		go.notify_all();
		rsx::detail::wait_for_zero_notification(remaining, [] { std::this_thread::yield(); });
		for (int i = 0; i < producers; ++i) check(results[i] == i + 1, "returned before producer completion");
		for (auto& worker : workers) worker.join();
	}
	std::cout << "PASS: immediate completion, short handoff, completion-before-park race, 200 rounds x 8 producers\n";
}

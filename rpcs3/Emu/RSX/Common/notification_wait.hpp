#pragma once

namespace rsx::detail
{
	// The condition is always rechecked. A completion between load() and wait()
	// cannot be lost because the atomic wait compares against the observed value.
	template <typename AtomicCounter, typename Pause>
	void wait_for_zero_notification(const AtomicCounter& counter, Pause pause)
	{
		// Avoid parking on the common short hand-off; bound the busy polling.
		for (unsigned spin = 0; spin < 64; ++spin)
		{
			if (counter.load() == 0)
			{
				return;
			}
			pause();
		}

		while (const auto observed = counter.load())
		{
			counter.wait(observed);
		}
	}
}

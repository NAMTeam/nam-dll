// Exercises the hook installers against scratch memory, so no game image is
// touched. The point of these tests is the guard rails: the expected-byte and
// expected-slot checks, and publishing the original before redirecting anything.
#include "doctest/doctest.h"
#include "Patching.h"

#include <Windows.h>
#include <cstring>
#include "wil/result_macros.h"

namespace
{
	constexpr uint8_t kPrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};

	// Frees the scratch page even when a check fails and unwinds.
	class ScratchPage final
	{
	public:
		ScratchPage()
			: page(static_cast<uint8_t*>(
				VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)))
		{
			REQUIRE(page != nullptr);
		}

		~ScratchPage()
		{
			if (page)
			{
				VirtualFree(page, 0, MEM_RELEASE);
			}
		}

		ScratchPage(const ScratchPage&) = delete;
		ScratchPage& operator=(const ScratchPage&) = delete;

		uint8_t* Get() const { return page; }

	private:
		uint8_t* page;
	};
}

TEST_CASE("an inline hook detours the entry and keeps the displaced bytes")
{
	const ScratchPage target;
	std::memcpy(target.Get(), kPrologue, sizeof(kPrologue));

	Patching::InlineHook hook{
		.address = reinterpret_cast<uint32_t>(target.Get()),
		.hookFunction = target.Get() + 32,
		.expectedBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8},
		.checkExpectedBytes = true
	};
	Patching::InstallInlineHook(hook);

	CHECK(hook.installed);
	CHECK(target.Get()[0] == 0xe9);
	REQUIRE(hook.trampoline != nullptr);
	CHECK(std::memcmp(hook.original.data(), kPrologue, sizeof(kPrologue)) == 0);
	CHECK(std::memcmp(hook.trampoline, kPrologue, sizeof(kPrologue)) == 0);

	SUBCASE("installing again is a no-op")
	{
		void* const trampoline = hook.trampoline;
		Patching::InstallInlineHook(hook);
		CHECK(hook.trampoline == trampoline);
	}
}

// The expected-byte check is what keeps a wrong address, or a different game
// build, from being detoured.
TEST_CASE("an inline hook refuses unexpected bytes and leaves the code alone")
{
	const ScratchPage target;
	std::memcpy(target.Get(), kPrologue, sizeof(kPrologue));
	target.Get()[2] = 0x00;

	Patching::InlineHook hook{
		.address = reinterpret_cast<uint32_t>(target.Get()),
		.hookFunction = target.Get() + 32,
		.expectedBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8},
		.checkExpectedBytes = true
	};

	CHECK_THROWS_AS(Patching::InstallInlineHook(hook), const wil::ResultException&);
	CHECK_FALSE(hook.installed);
	CHECK(hook.trampoline == nullptr);
	CHECK(target.Get()[0] == 0x55);
}

TEST_CASE("an inline hook can skip the byte check")
{
	const ScratchPage target;
	std::memset(target.Get(), 0x90, sizeof(kPrologue));

	Patching::InlineHook hook{
		.address = reinterpret_cast<uint32_t>(target.Get()),
		.hookFunction = target.Get() + 32,
		.expectedBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8},
		.checkExpectedBytes = false
	};
	Patching::InstallInlineHook(hook);

	CHECK(hook.installed);
	CHECK(target.Get()[0] == 0xe9);
}

// A hook has to be able to reach the game's implementation the instant the slot
// starts routing calls to it, so the original is published first.
TEST_CASE("a vtable hook replaces the expected slot and publishes the original")
{
	const ScratchPage page;
	auto* const slot = reinterpret_cast<void**>(page.Get());
	void* const original = page.Get() + 64;
	void* const replacement = page.Get() + 128;
	*slot = original;

	Patching::VTableHook hook{
		.slotAddress = reinterpret_cast<uint32_t>(slot),
		.hookFunction = replacement,
		.expectedOriginal = reinterpret_cast<uint32_t>(original)
	};
	Patching::InstallVTableHook(hook);

	CHECK(hook.installed);
	CHECK(hook.original == original);
	CHECK(*slot == replacement);

	SUBCASE("installing again does not capture the hook as the original")
	{
		Patching::InstallVTableHook(hook);
		CHECK(hook.original == original);
		CHECK(*slot == replacement);
	}
}

// A slot that no longer holds the expected function belongs to another build or
// another method, so overwriting it has to be refused.
TEST_CASE("a vtable hook refuses a slot holding something else")
{
	const ScratchPage page;
	auto* const slot = reinterpret_cast<void**>(page.Get());
	void* const somethingElse = page.Get() + 64;
	*slot = somethingElse;

	Patching::VTableHook hook{
		.slotAddress = reinterpret_cast<uint32_t>(slot),
		.hookFunction = page.Get() + 128,
		.expectedOriginal = reinterpret_cast<uint32_t>(page.Get() + 256)
	};

	CHECK_THROWS_AS(Patching::InstallVTableHook(hook), const wil::ResultException&);
	CHECK_FALSE(hook.installed);
	CHECK(hook.original == nullptr);
	CHECK(*slot == somethingElse);
}

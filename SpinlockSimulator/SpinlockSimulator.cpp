// SpinlockTest.cpp : Defines the entry point for the console application.
//

#include <stdio.h>
#include <tchar.h>
#include <Windows.h>
#include <assert.h>   
#include <process.h>

//make this global for easy access
LONG volatile lockValue = 1;
const UINT64 MIN_SPIN = 250;
const UINT64 MAX_SPIN = 1000000;
UINT64 globalSpins = 0;


BOOL GetLock(
	__in LONG Id)			// I Thread id and additional information
{
	assert(Id != 0);

	UINT64 oldId = InterlockedCompareExchangeAcquire(&lockValue, Id, 0);

	return oldId == 0;
}

void ReleaseLock( void *)
{
	//sleep an arbitrarily hard-coded time
	Sleep(5000);

	//set the value to 0 so the lock is released and spinlock acquires the lock
	lockValue = 0; 

}

void
SpinToAcquireLockWithExponentialBackoff(__in DWORD Id, __out UINT32 * BackoffCount)			
{

	UINT32	Backoffs = 0;

	UINT64 spinLimit = MIN_SPIN;

	UINT64 iSpin = 0;

	while (true)
	{
		assert(spinLimit <= MAX_SPIN && spinLimit >= MIN_SPIN);

		// Spin for a while without reading the lock.
		//
		for (iSpin = 0; iSpin < spinLimit; iSpin++)
		{
	
			_mm_pause(); 
		}

		//printf_s("iSpin = %ld\n", iSpin);

		//increment the global spins
		globalSpins = globalSpins + iSpin;

		//printf_s("globalSpins = %ld\n", globalSpins);


		// Try to get the lock.
		if (GetLock(Id))
		{
			break;
		}

		//
		spinLimit = min(MAX_SPIN, spinLimit * 2);

		// See if we need to backoff
		//
		Backoffs++;

	}

	//output the final backoffs
	*BackoffCount = Backoffs; 

} // SpinToAcquireLockWithExponentialBackoff


void ExerciseSpinLockCode()
{
	UINT32 backoffCount = 0;
	LARGE_INTEGER beforeHighRes, afterHighRes, elapsedMicroseconds, frequency;

	//spawn a thread to simulate lock release
	HANDLE hThread = (HANDLE)_beginthread(ReleaseLock, 0, NULL);

	//get start times
	QueryPerformanceCounter(&beforeHighRes);
	long int before = GetTickCount();
	
	//invoke Spinlock code
	SpinToAcquireLockWithExponentialBackoff(GetCurrentThreadId(), &backoffCount);
	
	//get the end times
	long int after = GetTickCount();
	QueryPerformanceCounter(&afterHighRes);
	QueryPerformanceFrequency(&frequency);

	//calculate microseconds
	//ticks divided by ticks-per-second (frequency) , gives us seconds
	//converted to microseconds by multiplying to 1 mil
	
	elapsedMicroseconds.QuadPart = (afterHighRes.QuadPart - beforeHighRes.QuadPart);
	elapsedMicroseconds.QuadPart *= 1000000;  
	elapsedMicroseconds.QuadPart /= frequency.QuadPart;

	UINT64 SpinsPerMillisecond = (globalSpins) / (after - before);

	printf_s("SpinToAcquireLockWithExponentialBackoff: Milliseconds elapsed = %ld, Spins=%I64d, Backoffs=%ld\n", after - before, globalSpins, backoffCount);
	printf_s("SpinToAcquireLockWithExponentialBackoff: Spins/Millisecond=%I64d\n", SpinsPerMillisecond);
	printf_s("SpinToAcquireLockWithExponentialBackoff: Spins/Millisecond(QPC)=%I64d\n", globalSpins/(elapsedMicroseconds.QuadPart/1000));

	//wait on the thread completion
	WaitForSingleObject(hThread, INFINITE);
}

void ExerciseSimpleLoopCode()
{
	long i=0;
	LARGE_INTEGER beforeHighRes, afterHighRes, elapsedMicroseconds, frequency;
	
	QueryPerformanceCounter(&beforeHighRes);

	//loop, no pause
	for (i; i < MAX_SPIN*10; i++)
	{
		;
	}


	QueryPerformanceCounter(&afterHighRes);
	QueryPerformanceFrequency(&frequency);

	
	//https://docs.microsoft.com/en-us/windows/desktop/SysInfo/acquiring-high-resolution-time-stamps
	elapsedMicroseconds.QuadPart = (afterHighRes.QuadPart - beforeHighRes.QuadPart);
	elapsedMicroseconds.QuadPart *= 1000000;
	elapsedMicroseconds.QuadPart /= frequency.QuadPart;


	long elapsedTimeQPC = elapsedMicroseconds.QuadPart / 1000 == 0 ? 1 : (elapsedMicroseconds.QuadPart / 1000);

	printf_s("Simple Loop: Millisecond elapsed (QPC)=%lld, Loops=%d\n", (elapsedMicroseconds.QuadPart / 1000), i);
	printf_s("Simple Loop: Spins/Millisecond (QPC)=%ld\n", i / elapsedTimeQPC);

}

int main()
{
	ExerciseSpinLockCode();

	ExerciseSimpleLoopCode();

	printf_s("Press ENTER to end...");
	_gettchar();

	return 0;
}


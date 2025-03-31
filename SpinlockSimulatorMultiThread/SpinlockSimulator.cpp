// SpinlockTest.cpp : Defines the entry point for the console application.
//

#include <stdio.h>
#include <tchar.h>
#include <Windows.h>
#include <assert.h>   
#include <process.h>
#include <vector>

//make this global for easy access
LONG volatile lockValue = 0;
const UINT64 MIN_SPIN = 250;
const UINT64 MAX_SPIN = 1000000;
//UINT64 globalSpins = 0;


BOOL GetLock(
	__in DWORD Id)			// I Thread id and additional information
{
	assert(Id != 0);

	//printf_s("Thread %d: Trying to acquire lock...\n", Id);

	LONG oldId = InterlockedCompareExchangeAcquire(&lockValue, Id, 0);

    
   // Check if the lock was acquired successfully and if the old value was 0 or not equal to Id, then the lock is free
   // If the old value is 0, it means the lock was free, and we acquired it.
   // If the old value is not equal to current thread Id, it means another thread has acquired the lock, and we need to spin again.
   if (oldId == 0)
   {
	   // Lock acquired successfully
	   //printf_s("Thread %d: Lock acquired successfully.\n", Id);
	   return TRUE;
   }
   else if (oldId != Id)
   {
	   // Another thread has the lock, so we need to spin
	   //printf_s("Thread %d: Lock is already held by another thread (%ld).\n", Id, oldId);
	   return FALSE;
   }
   else
   {
	   // The lock is held by the current thread, so we can proceed without spinning
	   //printf_s("Thread %d: Lock is already held by this current thread.\n", Id);
	   return TRUE;
   }


}

void SpinToAcquireLockWithExponentialBackoff(__in DWORD Id, __out UINT32* BackoffCount, __inout UINT64* localSpins)
{
    UINT32 Backoffs = 0;
    UINT64 spinLimit = MIN_SPIN;
    UINT64 iSpin = 0;

    while (true)
    {
        assert(spinLimit <= MAX_SPIN && spinLimit >= MIN_SPIN);

        // Spin for a while without reading the lock.
        for (iSpin = 0; iSpin < spinLimit; iSpin++)
        {
            _mm_pause();
        }

        // Increment the thread-local spins
        *localSpins += iSpin;

		//// Check if the spin limit has been reached
		//if (iSpin >= spinLimit)
		//{
		//	// Sleep for a short duration to yield the CPU
		//	Sleep(0);
		//}

		// Try to get the lock.
        if (GetLock(Id))
        {
            break;
        }

        // Exponential backoff
        spinLimit = min(MAX_SPIN, spinLimit * 2);
        Backoffs++;
    }

    // Output the final backoffs
    *BackoffCount = Backoffs;
}

void ExerciseSpinLockCode(UINT64* localSpins)
{
    UINT32 backoffCount = 0;
    LARGE_INTEGER beforeHighRes, afterHighRes, elapsedMicroseconds, frequency;
    DWORD threadId = GetCurrentThreadId();

    // Get start times
    QueryPerformanceCounter(&beforeHighRes);
    long int before = GetTickCount();

    // Invoke Spinlock code
    SpinToAcquireLockWithExponentialBackoff(GetCurrentThreadId(), &backoffCount, localSpins);

	 // Simulate some work while holding the lock
    int seconds = 3000;
	printf_s("Thread %d acquired the lock. Simulating work by sleeping %d seconds...\n", threadId, seconds/1000);
	Sleep(seconds); // Simulate holding the lock for 3 second
 
    // Release the lock atomically after acquiring it
    InterlockedExchange(&lockValue, 0);

    // Get the end times
    long int after = GetTickCount();
    QueryPerformanceCounter(&afterHighRes);
    QueryPerformanceFrequency(&frequency);

    // Calculate microseconds
    elapsedMicroseconds.QuadPart = (afterHighRes.QuadPart - beforeHighRes.QuadPart);
    elapsedMicroseconds.QuadPart *= 1000000;
    elapsedMicroseconds.QuadPart /= frequency.QuadPart;

    UINT64 SpinsPerMillisecond = (*localSpins) / (after - before);

    printf_s("Thread %d: SpinToAcquireLockWithExponentialBackoff: Milliseconds elapsed = %ld, Spins=%llu, Backoffs=%ld\n", threadId, after - before, *localSpins, backoffCount);
    printf_s("Thread %d: SpinToAcquireLockWithExponentialBackoff: Spins/Millisecond=%llu\n", threadId, SpinsPerMillisecond);
    printf_s("Thread %d: SpinToAcquireLockWithExponentialBackoff: Spins/Millisecond(QPC)=%llu\n", threadId , *localSpins / (elapsedMicroseconds.QuadPart / 1000));
}

void ExerciseSimpleLoopCode()
{
	long i = 0;
	LARGE_INTEGER beforeHighRes, afterHighRes, elapsedMicroseconds, frequency;
    DWORD threadId = GetCurrentThreadId();

	QueryPerformanceCounter(&beforeHighRes);

	//loop, no pause
	for (i; i < MAX_SPIN * 10; i++)
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

	printf_s("Thread %d: Simple Loop - Millisecond elapsed (QPC)=%lld, Loops=%d\n", threadId, (elapsedMicroseconds.QuadPart / 1000), i);
	printf_s("Thread %d: Simple Loop - Spins/Millisecond (QPC)=%ld\n", threadId, i / elapsedTimeQPC);

}


struct ThreadData
{
    UINT64 localSpins; // Thread-local spins
};

unsigned __stdcall ThreadFunction(void* param)
{
    ThreadData* data = (ThreadData*)param;

    // Initialize thread-local spins
    data->localSpins = 0;

    printf_s("Thread %d started.\n", GetCurrentThreadId());

    // Call ExerciseSpinLockCode and ExerciseSimpleLoopCode
	ExerciseSpinLockCode(&data->localSpins);
    ExerciseSimpleLoopCode();

    printf_s("Thread %d completed. Local Spins: %llu\n", GetCurrentThreadId(), data->localSpins);

    return 0;
}

int main(int argc, char* argv[])
{
    // Default number of threads
    int numThreads = 4;

    // If a parameter is provided, parse it as the number of threads
    if (argc > 1)
    {
        numThreads = atoi(argv[1]);
        if (numThreads <= 0)
        {
            printf_s("Invalid number of threads specified. Using default: 4 threads.\n");
            numThreads = 4;
        }
    }

    printf_s("Launching %d thread(s)...\n", numThreads);

    // Vector to store thread handles and thread data
    std::vector<HANDLE> threadHandles;
    std::vector<ThreadData> threadData(numThreads);

    // Create threads
    for (int i = 0; i < numThreads; ++i)
    {
        HANDLE hThread = (HANDLE)_beginthreadex(
            NULL,                // Security attributes
            0,                   // Stack size (default)
            ThreadFunction,      // Thread function
            &threadData[i],      // Parameter to pass to the thread
            0,                   // Creation flags
            NULL                 // Thread ID (not used)
        );

        if (hThread == NULL)
        {
            printf_s("Failed to create thread %d\n", i + 1);
        }
        else
        {
            threadHandles.push_back(hThread);
        }
    }

    // Wait for all threads to complete
    if (!threadHandles.empty())
    {
        WaitForMultipleObjects(
            static_cast<DWORD>(threadHandles.size()), // Number of handles
            threadHandles.data(),                    // Array of handles
            TRUE,                                    // Wait for all threads
            INFINITE                                 // Timeout
        );

        // Close all thread handles
        for (HANDLE hThread : threadHandles)
        {
            CloseHandle(hThread);
        }
    }

    // Aggregate results from all threads
    UINT64 totalSpins = 0;
    for (const auto& data : threadData)
    {
        totalSpins += data.localSpins;
    }

    printf_s("All threads completed. Total Spins: %llu\n", totalSpins);
    printf_s("Press ENTER to end...");
    _gettchar();

    return 0;
}
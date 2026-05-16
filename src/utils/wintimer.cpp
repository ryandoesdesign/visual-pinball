// license:GPLv3+

#include "core/stdafx.h"
#include <ctime>
//#ifndef _MSC_VER
//#include <unistd.h>
//#endif

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

FrameProfiler* g_frameProfiler;

//#define USE_LOWLEVEL_PRECISION_SETTING // does allow to pick lower windows timer resolutions than 1ms (usually 0.5ms as of win10/2020) via undocumented API calls, BUT lead to sound distortion on some setups in PinMAME, so also disable it in VPX for now

#ifdef USE_LOWLEVEL_PRECISION_SETTING
typedef LONG(CALLBACK* NTSETTIMERRESOLUTION)(IN ULONG DesiredTime,
	IN BOOLEAN SetResolution,
	OUT PULONG ActualTime);
static NTSETTIMERRESOLUTION NtSetTimerResolution;

typedef LONG(CALLBACK* NTQUERYTIMERRESOLUTION)(OUT PULONG MaximumTime,
	OUT PULONG MinimumTime,
	OUT PULONG CurrentTime);
static NTQUERYTIMERRESOLUTION NtQueryTimerResolution;

static HMODULE hNtDll = nullptr;
static ULONG win_timer_old_period = -1;
#endif


void set_lowest_possible_win_timer_resolution()
{
}

void restore_win_timer_resolution()
{
	// restore both timer resolutions
#ifdef USE_LOWLEVEL_PRECISION_SETTING
	if (hNtDll) {
		if (win_timer_old_period != -1)
		{
			ULONG tmp;
			NtSetTimerResolution(win_timer_old_period, FALSE, &tmp);
			win_timer_old_period = -1;
		}
		FreeLibrary(hNtDll);
		hNtDll = nullptr;
	}
#endif

}

//

static unsigned int sTimerInit = 0;
static LARGE_INTEGER TimerFreq = {};
static LARGE_INTEGER sTimerStart = {};
static LONGLONG OneMSTimerTicks = 0;
static LONGLONG TwoMSTimerTicks = 0;
static char highrestimer = 0;

// call before 1st use of msec,usec or uSleep
void wintimer_init()
{
   sTimerInit = 1;

#ifdef _MSC_VER
   QueryPerformanceFrequency(&TimerFreq);
   QueryPerformanceCounter(&sTimerStart);

   HANDLE timer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS); // ~0.5msec resolution (unless usec < ~10 requested, which most likely triggers a spin loop then), Win10 and above only, note that this timer variant then also would not require to call timeBeginPeriod(1) before!
   highrestimer = !!timer;
   if (timer)
      CloseHandle(timer);
#else
   TimerFreq.QuadPart = SDL_GetPerformanceFrequency();
   sTimerStart.QuadPart = SDL_GetPerformanceCounter();

   highrestimer = 0; //!! ???
#endif

   OneMSTimerTicks = (1000 * TimerFreq.QuadPart) / 1000000ull;
   TwoMSTimerTicks = (2000 * TimerFreq.QuadPart) / 1000000ull;
}

uint64_t usec()
{
   if (sTimerInit == 0) return 0;

   LARGE_INTEGER TimerNow;
#ifdef _MSC_VER
   QueryPerformanceCounter(&TimerNow);
#else
   TimerNow.QuadPart = SDL_GetPerformanceCounter();
#endif
   const uint64_t cur_tick = (uint64_t)(TimerNow.QuadPart - sTimerStart.QuadPart);
   return ((uint64_t)TimerFreq.QuadPart < 100000000ull) ? (cur_tick * 1000000ull / (uint64_t)TimerFreq.QuadPart)
      : (cur_tick * 1000ull / ((uint64_t)TimerFreq.QuadPart / 1000ull));
}

uint32_t msec()
{
   if (sTimerInit == 0) return 0;

   LARGE_INTEGER TimerNow;
#ifdef _MSC_VER
   QueryPerformanceCounter(&TimerNow);
#else
   TimerNow.QuadPart = SDL_GetPerformanceCounter();
#endif
   const LONGLONG cur_tick = TimerNow.QuadPart - sTimerStart.QuadPart;
   return (uint32_t)((uint64_t)cur_tick * 1000ull / (uint64_t)TimerFreq.QuadPart);
}

// tries(!) to be as exact as possible at the cost of potentially causing trouble with other threads/cores due to OS madness
// needs timeBeginPeriod(1) before calling 1st time to make the Sleep(1) in here behave more or less accurately (and timeEndPeriod(1) after not needing that precision anymore)
// but VP code does this already
void uSleep(const uint64_t u)
{
//#ifdef _MSC_VER
   if (sTimerInit == 0) return;

   LARGE_INTEGER TimerNow;
#ifdef _MSC_VER
   QueryPerformanceCounter(&TimerNow);
#else
   TimerNow.QuadPart = SDL_GetPerformanceCounter();
#endif
   LARGE_INTEGER TimerEnd;
   TimerEnd.QuadPart = TimerNow.QuadPart + ((u * TimerFreq.QuadPart) / 1000000ull);

   while (TimerNow.QuadPart < TimerEnd.QuadPart)
   {
      if ((TimerEnd.QuadPart - TimerNow.QuadPart) > TwoMSTimerTicks)
         Sleep(1); // really pause thread for 1-2ms (depending on OS)
#ifdef _MSC_VER
      else if (highrestimer && ((TimerEnd.QuadPart - TimerNow.QuadPart) > OneMSTimerTicks)) // pause thread for 0.5-1ms
      {
         HANDLE timer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS); // ~0.5msec resolution (unless usec < ~10 requested, which most likely triggers a spin loop then), Win10 and above only, note that this timer variant then also would not require to call timeBeginPeriod(1) before!
         LARGE_INTEGER ft;
         ft.QuadPart = -10 * 500; // 500 usec //!! we could go lower if some future OS (>win10) actually supports this
         SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
         WaitForSingleObject(timer, INFINITE);
         CloseHandle(timer);
      }
#endif
      else
         YieldProcessor(); // was: "SwitchToThread() let other threads on same core run" //!! could also try Sleep(0) or directly use _mm_pause() instead of YieldProcessor() here

#ifdef _MSC_VER
      QueryPerformanceCounter(&TimerNow);
#else
      TimerNow.QuadPart = SDL_GetPerformanceCounter();
#endif
   }
//#else
//   usleep(u); // could use udelay or nanosleep instead of usleep if usec < ~10 needed! //!! seems like in practice, usleep is also not very precise AND nanosleep too CPU heavy, all very OS/platform dependent
//#endif
}

//

static constexpr unsigned int daysPerMonths[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; // Number of days per month

// (Rough) angle of the day (radian)
double AngleOfDay(const unsigned int day, const unsigned int month, const unsigned int year)
{
    bool leapYear;
    unsigned int totalDaysInYear;
    if ((year % 400) == 0)
    {
        totalDaysInYear = 366;
        leapYear = true;
    }
    else if ((year % 100) == 0)
    {
        totalDaysInYear = 365;
        leapYear = false;
    }
    else if ((year % 4) == 0)
    {
        totalDaysInYear = 366;
        leapYear = true;
    }
    else
    {
        totalDaysInYear = 365;
        leapYear = false;
    }

    unsigned int numOfDays = 0;
    for (unsigned int i = 1; i < month; i++)
        numOfDays += daysPerMonths[i-1];
    if ((month > 2) && leapYear)
        numOfDays++;
    numOfDays += day;

    return ((2. * M_PI)*(numOfDays - 1)) / totalDaysInYear;
}

double SolarDeclination(const double dayAngle) // radian
{
    const double c = cos(dayAngle);
    const double s = sin(dayAngle);
    return 0.006918
         - 0.399912 * c
         + 0.070257 * s
         - 0.006758 * (2.*c*c - 1.)
         + 0.000907 * 2.*c*s
         - 0.002697 * (c*(4.*c*c - 3.))
         + 0.00148  * (s*(-4.*s*s + 3.));
}

double EquationOfTimeRadian(const double dayAngle) // radian
{
    const double c = cos(dayAngle);
    const double s = sin(dayAngle);
    return 0.000075
         + 0.001868 * c
         - 0.032077 * s
         - 0.014615 * (2.*c*c - 1.)
         - 0.04089  * 2.*c*s;
}

inline double DayDurationHalfRadian(const double declination, const double rlat) // radian, result radian*0.5
{
    return acos(-tan(rlat) * tan(declination));
}

inline double DayDurationHours(const double declination, const double rlat) // radian
{
    return DayDurationHalfRadian(declination, rlat) * (24. / M_PI);
}

// Decimal hour of sunset/sunrise: result in universal time
double SunsetSunriseUniversalTime(const unsigned int day, const unsigned int month, const unsigned int year, const double rlong, const double rlat, const bool sunrise) // longitude in radians (positive east)
{
    const double dayAngle = AngleOfDay(day, month, year);
    const double ddh = fabs(DayDurationHours(SolarDeclination(dayAngle), rlat));

    return 12. + (sunrise ? -0.5 : 0.5)*ddh - (rlong + EquationOfTimeRadian(dayAngle)) * (12. / M_PI);
}

double LocalTimeAdjust()
{
    time_t hour_machine;
    time(&hour_machine);
    tm gmt_hour;
    gmtime_s(&gmt_hour, &hour_machine);
    tm local_hour;
    localtime_s(&local_hour, &hour_machine);

    const int dif = local_hour.tm_hour - gmt_hour.tm_hour;
    return (dif < -12) ? dif + 24 : dif;
}

// Decimal hour of sunset/sunrise: result in local hour
double SunsetSunriseLocalTime(const unsigned int day, const unsigned int month, const unsigned int year, const double rlong, const double rlat, const bool sunrise) // longitude in radians (positive east)
{
	return SunsetSunriseUniversalTime(day, month, year, rlong, rlat, sunrise) + LocalTimeAdjust();
}

double OrbitalExcentricity(const double dayAngle)
{
	const double c = cos(dayAngle);
	const double s = sin(dayAngle);
	return 1.000110
		+ 0.034221 * c
		+ 0.001280 * s
		+ 0.000719 * (2.*c*c - 1.)
		+ 0.000077 * 2.*c*s;
}

// Theoretical energy flux for the day radiation
double TheoreticRadiation(const unsigned int day, const unsigned int month, const unsigned int year, const double rlat) // radian
{
	const double dayAngle = AngleOfDay(day, month, year);
	const double declination = SolarDeclination(dayAngle);
	const double e0 = OrbitalExcentricity(dayAngle);
	const double sunriseHourAngle = DayDurationHalfRadian(declination, rlat);

	const double c0 = cos(declination - rlat);
	const double c1 = cos(declination + rlat);
	// Theoretical radiation in W.m-2
	constexpr double solarConst = 1367.; // solar constant W.m-2
	return 0.5 * solarConst * e0 * ((c0 + c1)*sin(sunriseHourAngle) / sunriseHourAngle + c0 - c1);
}

// Max/Year Theoretical energy flux for the day radiation
double MaxTheoreticRadiation(const unsigned int year, const double rlat) // radian
{
    double maxTR = 0.;
    for (unsigned int month = 0; month < 12; ++month)
        for (unsigned int day = 0; day < daysPerMonths[month]; ++day)
        {
            const double TR = TheoreticRadiation(day,month,year,rlat);
            if (TR > maxTR)
                maxTR = TR;
        }
    return maxTR;
}


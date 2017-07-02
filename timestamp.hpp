#pragma once

#include <sys/types.h>
#include <sys/time.h>

class Timestamp {
public:
	Timestamp()
    {
        reset();
	}
    
    void reset()
    {
        now(start_);
    }

	double elapsed_seconds() const
    {
		timeval end;
        now(end);
		return double (end.tv_sec - start_.tv_sec) + double (end.tv_usec - start_.tv_usec) / 1000000.0;
	}

	double elapsed_millis() const
    {
		timeval end;
		now(end);
		return double (end.tv_sec - start_.tv_sec) * 1000.0 + double (end.tv_usec - start_.tv_usec) / 1000.0;
	}

	double elapsed_micros() const
    {
		timeval end;
        now(end);
		return double (end.tv_sec - start_.tv_sec) * 1000000.0 + double (end.tv_usec - start_.tv_usec);
	}
private:
    static void now( timeval &tv )
    {
        gettimeofday(&tv, NULL);
    }
private:
    timeval start_;
};

//
//  testException.c
//  CObjects
//
//  Created by George Boumis on 13/12/13.
//  Copyright (c) 2013 George Boumis. All rights reserved.
//

#include <stdio.h>
#include <cobj.h>
#include <setjmp.h>
#include <pthread.h>

#define ExceptionDivideByZeroCode 42
struct exception_t ExceptionDivideByZero = {
	"ExceptionDivideByZero",
	"You made a division by zero",
	ExceptionDivideByZeroCode
};

void *threadf(void *arg);

struct thread_data {
	size_t id;
};

int main () {	
	size_t threads = 10;
	pthread_t pthreads[threads];
	struct  thread_data pthreadsData[threads];
	for (size_t i=0; i<threads; i++) {
		pthreadsData[i].id = i;
		pthread_create(&pthreads[i], NULL, threadf, (void *)&pthreadsData[i]);
	}
	
	for (size_t i=0; i<threads; i++)
		pthread_join(pthreads[i], NULL);
	
	COTRY {
		
	}
	COOTHER {
		
	}
	COEND;
	return 0;
}

void *threadf(void *arg) {
	struct thread_data *data = (struct thread_data *)arg;
	COTRY {
		COTRY {
			puts("Throw");
			COTHROW((int)data->id, "COTestException", "there is no reason");
		}
		COCATCH(1) {
			printf("Nested catch by %x\n", (int)pthread_self());
			COHANDLE();
			CORAISE(&ExceptionDivideByZero);
		}
		COOTHER {
			printf("Some unhandled exception nested by %x\n", (int)pthread_self());
		}
		COFINALLY {
			printf("Nested finally by %x\n", (int)pthread_self());
		}
		COEND
	}
	COCATCH(1) {
		printf("Outher handled exception %x\n", (int)pthread_self());
		COHANDLE();
	}
	COCATCH(ExceptionDivideByZeroCode) {
		printf("Outer catch by %x\n", (int)pthread_self());
		COExceptionLog(COCurrentException());
		COHANDLE();
	}
	COOTHER {
		printf("Outer some unhandled exception by %x\n", (int)pthread_self());
		COExceptionLog(COCurrentException());
		COHANDLE();
	}
	COFINALLY {
		printf("Outer Finally by %x\n", (int)pthread_self());
	}
	COEND
	return NULL;
}

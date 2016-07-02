#include <assert.h>
#include <math.h>     /* Included in case you use the standard math library. */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

unsigned int	count_distinct_factors(unsigned int n);
unsigned int	count_factors(unsigned int n);
unsigned int	count_factors_recursive(unsigned int n);
void		test_factors(void);
unsigned int    next_factor(unsigned int x, unsigned int k);
int             isPrime(unsigned int x);
unsigned int    prime_array[9] = {2,3,5,7,11,13,17,19,23};

/*
 * A helper function to check whether a given integer is prime number.
 * Requires:
 * Integer input x greater than 1;
 * 
 * Effects: 
 * Output 0 if not prime, 1 if prime.
 */
int
isPrime(unsigned int x)
{
	assert(x > 1);
	unsigned int k;
	for (k = 2; k < x; k++) {
		if (x % k == 0)
			return (0);
	}
	return (1);
}

/* 
 * A helper function to recursively keep track of number of prime factors.
 * Requires:
 *  Integer input x greater than 1; integer factor k.
 *
 * Effects:
 *  Modifies the number of factors of input x.
 */
unsigned int
next_factor(unsigned int x, unsigned int k)
{
	if (x == 1)
		return 0;     /* 1 is not a prime number. */
	else if (x % k == 0) 
		return  next_factor(x / k, k) + 1;
	/* If divisible, then contains one new prime factor. */
	else {
		if (k <= sqrt(x))
			return  next_factor(x, k + 1);
		else    
		/* If the trial factor exceeds sqrt, no hope! it's prime */
			return 1;
	} /* Increment the factorizer, to check if any prime factor exists. */
}

/* 
 * Requires:  
 *   The input "n" must be greater than 1.
 *
 * Effects: 
 *   Returns the number of factors of the input "n".
 */
unsigned int
count_factors_recursive(unsigned int n)
{
	assert(n > 1);
	return next_factor(n,2);    
	/* Call next_factor to recursively count prime factors.*/
}

/* 
 * Requires:  
 *   The input "n" must be greater than 1.
 *
 * Effects: 
 *   Returns the number of factors of the input "n".
 */
unsigned int
count_factors(unsigned int n)
{
	unsigned int f = 2;
	unsigned int num = 0;
	/* Initially factorizer is 2, and number of prime factors is 0. */
	assert(n > 1);
     	while (n != 1) {
		if (n % f == 0) {
			num++;
			n = n/f;  
                /* If divisible, check the next prime factor. */
		} else
			f++;  /* Increment factor and keep trying. */
	}
	return (num);
}

/* 
 * Requires:  
 *   The input "n" must be greater than 1.
 *
 * Effects: 
 *   Returns the number of distinct factors of the input "n".
 */
unsigned int
count_distinct_factors(unsigned int n)
{
	unsigned int f = 2;
	unsigned int k = 0;     /* A flag to keep track of distinct numbers. */
	unsigned int num = 0;
	/* Initial condition is the same as the one above. */
	assert(n > 1);
       	while (n != 1) {
		if (n % f == 0) { 
			if (f != k)
				num++;    
                        /* Only count for distinct numbers. */
			n = n/f;
			k = f;
		
		} else
			f++;
	}
	return (num);
}

/* 
 * Requires:
 *   Nothing.
 *
 * Effects:
 *   Runs testing procedures.
 */
void
test_factors(void)
{
	printf("Testing Factors......\n");
	if ((count_factors_recursive(2) == count_factors(2) == 
	     count_distinct_factors(2) == 1) && 
	    (count_factors_recursive(5465460) == 
	     count_factors(5465460) == 9) &&
	    (count_distinct_factors(5465460) == 6))
		printf("Simple tests passed\n");
	unsigned int i;
	unsigned int test_num = 1;
	for (i = 1; i < 25; i++) {
		test_num *= 2;
		if (count_factors(test_num) != i || 
		    count_factors_recursive(test_num) != i)
			printf("Wrong number of all prime factors!\n");
		if (count_distinct_factors(test_num) != 1)
			printf("Problem finding distinct prime factors\n");
	}       /* Checks the composite with factors of same numbers.*/
	printf("One dimension identical prime factors test finished\n");
	for (i = 2; i < 20000; i++) {
		if (isPrime(i) == 1) {     /* Check only the prime numbers.*/
			assert(count_factors_recursive(i) == 1);
		      	assert(count_distinct_factors(i) == 1);
	       		assert(count_factors(i) == 1);
		}
		if (i % 1000== 0) 
			printf(".");
	}  /* All prime numbers should only have one factor, which is itself.*/
	printf("\n One dimension prime numbers test finished\n");
	unsigned int j;
	for (i = 0; i < 9; i++) {
		test_num = 1;
		for (j = 0; j <= i; j++) {
			test_num *= prime_array[j];
		}
		unsigned int factor_num = count_factors(test_num);
      		if (factor_num == j && count_factors_recursive(test_num) == j)
			continue;
		else
			printf("Problem finding prime factors for %u\n",
			       test_num);
	}
	/* Construct the composite and checks if number of factors is correct.*/
	printf("General prime factors test finished\n");
	unsigned int k;
	unsigned int test_fact;
	for (i = 0; i < 4; i++) {
		test_num = 1;
		for (j = 0; j <= i; j++) {
			test_fact = prime_array[j];
			unsigned int factor = 1;
			for (k = 0; k < 3; k++) {
				factor *= test_fact;
			}
			test_num *= factor;
	        }
		if (!(count_distinct_factors(test_num) == j &&
		      count_factors_recursive(test_num) == 3*j && 
		      count_factors(test_num) == 3*j))
			printf("Problem finding distinct prime factors for %u\n"
			       ,test_num);
	}       
	/* Checks numbers composite by multiple prime numbers and multiple 
	   occurrence.*/
	printf("Distinct prime factors test finished\n");
}

/* 
 * Requires:
 *   Nothing.
 *
 * Effects:
 *   If the "-t" option is specified on the command line, then test code is
 *   executed and the program exits.
 *   
 *   Otherwise, requests a number from standard input.  If the "-r" option is
 *   specified on the command line, then prints the number of prime factors
 *   that the input number has, calculated using a recursive function.
 *   Otherwise, prints the number of prime factors that the input number has
 *   and the number of those factors that are distinct using iterative
 *   functions.
 *
 *   Upon completion, the program always returns 0.
 *
 *   If the number that is input is not between 2 and the largest unsigned
 *   integer, the output of the program is undefined, but it will not crash.
 */
int
main(int argc, char **argv)
{
	unsigned int n;
	int c;
	bool runtests = false;
	bool recursive = false;

	/* Parse the command line. */
	while ((c = getopt(argc, argv, "tr")) != -1) {
		switch (c) {
		case 't':             /* Run test procedure and exit. */
			runtests = true;
			break;
		case 'r':             /* Use recursive version. */
			recursive = true;
			break;
		default:
			break;
		}
	}

	/* If "-t" is specified, run test procedure and exit program. */
	if (runtests) {
		test_factors();
		return (0);
	}

	/* Get input. */
	printf("Enter number:\n");
	scanf("%u", &n);

	/* Print results. */
	if (recursive) {
		/* Use recursive version. */
		printf("%u has %u prime factors.\n",
		    n, count_factors_recursive(n));
	} else {
		/* Use iterative versions. */
		printf("%u has %u prime factors, %u of them distinct.\n",
		    n, count_factors(n), count_distinct_factors(n));
	}

	/* No errors. */
	return (0);
}

/*
 * The last lines of this file configure the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */

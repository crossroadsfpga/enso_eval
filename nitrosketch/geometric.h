#ifndef GEOMETRIC_H
#define GEOMETRIC_H

#include <limits.h>
#include <math.h>
#include <stdlib.h>

/* Geometric distribution (bernoulli trial with probability p)
   prob(k) =  p (1 - p)^(k-1) for n = 1, 2, 3, ...
   It gives the distribution of "waiting times" for an event that
   occurs with probability p. */

#ifdef FASTRAND_UNI
double randArr[1 << 16];
unsigned short randIdx;
#endif

#ifdef FASTRAND_GEO
uint32_t geoArr[1 << 16];
unsigned short geoIdx = 0;
#endif


unsigned int
ran_geometric (const double p)
{
  #ifdef FASTRAND_UNI
  double u = randArr[++randIdx];
  #else
  double u = (double)rand()/RAND_MAX;
  #endif

  unsigned int k;

  if (p == 1.0)
    {
      k = 1;
    }
  else
    {
      k = log (u) / log (1 - p) + 1;
    }
  return k;
}

double
ran_geometric_pdf (const unsigned int k, const double p)
{
  if (k == 0)
    {
      return 0 ;
    }
  else if (k == 1)
    {
      return p ;
    }
  else
    {
      double P = p * pow (1 - p, k - 1.0);
      return P;
    }
}


int MinOfFive(int a, int b, int c, int d, int e)
{
    int min = INT_MAX;
    if (a<min)
    {
       min = a;
    }
    if (b<min)
    {
       min = b;
    }
    if (c<min)
    {
       min = c;
    }
    if (d<min)
    {
       min = d;
    }
    if (e<min)
    {
       min = e;
    }

    return min;
}


int MedianOfFive(int a, int b, int c, int d, int e)
{
    return b < a ? d < c ? b < d ? a < e ? a < d ? e < d ? e : d
    : c < a ? c : a
    : e < d ? a < d ? a : d
    : c < e ? c : e
    : c < e ? b < c ? a < c ? a : c
    : e < b ? e : b
    : b < e ? a < e ? a : e
    : c < b ? c : b
    : b < c ? a < e ? a < c ? e < c ? e : c
    : d < a ? d : a
    : e < c ? a < c ? a : c
    : d < e ? d : e
    : d < e ? b < d ? a < d ? a : d
    : e < b ? e : b
    : b < e ? a < e ? a : e
    : d < b ? d : b
    : d < c ? a < d ? b < e ? b < d ? e < d ? e : d
    : c < b ? c : b
    : e < d ? b < d ? b : d
    : c < e ? c : e
    : c < e ? a < c ? b < c ? b : c
    : e < a ? e : a
    : a < e ? b < e ? b : e
    : c < a ? c : a
    : a < c ? b < e ? b < c ? e < c ? e : c
    : d < b ? d : b
    : e < c ? b < c ? b : c
    : d < e ? d : e
    : d < e ? a < d ? b < d ? b : d
    : e < a ? e : a
    : a < e ? b < e ? b : e
    : d < a ? d : a;
}


long double MedianOfFiveL(long double a, long double b, long double c, long double d, long double e)
{
    return b < a ? d < c ? b < d ? a < e ? a < d ? e < d ? e : d
    : c < a ? c : a
    : e < d ? a < d ? a : d
    : c < e ? c : e
    : c < e ? b < c ? a < c ? a : c
    : e < b ? e : b
    : b < e ? a < e ? a : e
    : c < b ? c : b
    : b < c ? a < e ? a < c ? e < c ? e : c
    : d < a ? d : a
    : e < c ? a < c ? a : c
    : d < e ? d : e
    : d < e ? b < d ? a < d ? a : d
    : e < b ? e : b
    : b < e ? a < e ? a : e
    : d < b ? d : b
    : d < c ? a < d ? b < e ? b < d ? e < d ? e : d
    : c < b ? c : b
    : e < d ? b < d ? b : d
    : c < e ? c : e
    : c < e ? a < c ? b < c ? b : c
    : e < a ? e : a
    : a < e ? b < e ? b : e
    : c < a ? c : a
    : a < c ? b < e ? b < c ? e < c ? e : c
    : d < b ? d : b
    : e < c ? b < c ? b : c
    : d < e ? d : e
    : d < e ? a < d ? b < d ? b : d
    : e < a ? e : a
    : a < e ? b < e ? b : e
    : d < a ? d : a;
}

#endif // GEOMETRIC_H

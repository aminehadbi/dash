/* 
 * hello/main.cpp 
 *
 * author(s): Karl Fuerlinger, LMU Munich */
/* @DASH_HEADER@ */

#include <unistd.h>
#include <iostream>
#include <libdash.h>

#define SIZE 10

using namespace std;

int main(int argc, char* argv[])
{
  dash::init(&argc, &argv);
  
  auto myid = dash::myid();
  auto size = dash::size();

  dash::Array< dash::GlobPtr<int> > arr(size);

  arr[myid] = dash::memalloc<int>(SIZE);
  
  for(int i=0; i<SIZE; i++ ) {
    dash::GlobPtr<int> ptr = arr[myid];
    ptr[i]=myid;
  }
  
  dash::barrier();

  cout<<myid<<": ";
  for(int i=0; i<SIZE; i++ ) {
    dash::GlobPtr<int> ptr = arr[(myid+1)%size];
    cout<<ptr[i]<<" ";
  }
  cout<<endl;

  dash::barrier();

  //dash::memfree(arr[myid]);
  
  dash::finalize();
}

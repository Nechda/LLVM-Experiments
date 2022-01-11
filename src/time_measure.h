#pragma once 

#include <chrono>
#include <iostream>


struct TimeMeasure {                                                                                                                                     
  public:
    TimeMeasure()
    {   
        begin_ = std::chrono::steady_clock::now();
    }
    void stop()
    {
        auto end_ = std::chrono::steady_clock::now();
        diff_ = std::chrono::duration_cast<std::chrono::microseconds>(end_ - begin_).count();
    }
    auto get() { return diff_; } 
    void print()
    {
        std::cout << "Time difference = " << diff_/1e+6 << " [s]" << std::endl;
    }
  private:
    std::chrono::steady_clock::time_point begin_;
    std::chrono::steady_clock::time_point end_;
    size_t diff_ = 0;
};

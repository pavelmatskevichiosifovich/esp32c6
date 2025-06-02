#ifndef L298N_H
#define L298N_H

namespace l298n {
    void init();
    void forward();
    void backward();
    void stop();
    void startCycleTask();
}

#endif
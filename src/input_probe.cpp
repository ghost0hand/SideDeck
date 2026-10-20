// Deliberately a separate executable, never on the HidHide application list.
#include "gamepad.hpp"
int main() {
    gp::Input input; if (!input.available()) return 128;
    input.poll(); int mask=0;
    for(int i=0;i<gp::MAX_PADS;++i) if(input.connected(i))mask|=1<<i;
    return mask;
}

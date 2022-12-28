#ifndef SELECTIONSUMMARY_H
#define SELECTIONSUMMARY_H

#include "Selection.h"

namespace Gui{

class SelectionSummary: public Gui::SelectionObserver{
    public:
        void onSelectionChanged(const Gui::SelectionChanges& msg);
};

}

#endif // SELECTIONSUMMARY_H

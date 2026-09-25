#include "modules/folderswitch/FolderSwitchLogic.h"

namespace FolderSwitchLogic {

bool isManagerFresh(qint64 lastSeenMs, qint64 nowMs)
{
    return (nowMs - lastSeenMs) < kManagerFreshnessMs;
}

bool shouldAutoSwitch(bool autoSwitchOn, bool masterOn, bool managerFresh, bool isPendingReturn,
                      bool alreadySwitchedThisDialog)
{
    if (alreadySwitchedThisDialog) {
        return false;
    }
    return autoSwitchOn && masterOn && managerFresh && isPendingReturn;
}

} // namespace FolderSwitchLogic

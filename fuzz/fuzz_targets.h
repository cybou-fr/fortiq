#pragma once

#include <QByteArray>

namespace fortiq::fuzz {
int targetCount();
const char *targetName(int index);
void runTarget(int index, const QByteArray &data);   // aborts on a broken invariant
}

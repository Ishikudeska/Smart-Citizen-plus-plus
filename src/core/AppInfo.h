#pragma once

#include <QString>

namespace core {

// Stamps name, organization and version onto the running application.
// Call after the Q*Application is constructed.
void applyIdentity();

// One-line description of the build, for the log header and About page.
QString buildDescription();

} // namespace core

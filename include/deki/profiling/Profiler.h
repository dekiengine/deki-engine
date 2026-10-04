#pragma once

// Profiling macros for marking code paths. They expand to nothing in this
// build.
//
//   void MyFunction() {
//       DEKI_PROFILE_SCOPE;  // profiles this function
//       // ...
//   }
//
//   void MyNamedSection() {
//       {
//           DEKI_PROFILE_SCOPE_N("CustomSection");
//           // ... profiled section
//       }
//   }

#define DEKI_PROFILE_SCOPE
#define DEKI_PROFILE_SCOPE_N(name)
#define DEKI_PROFILE_FRAME
#define DEKI_PROFILE_FRAME_N(name)
#define DEKI_PROFILE_PLOT(name, value)
#define DEKI_PROFILE_MESSAGE(text, size)
#define DEKI_PROFILE_SCOPE_COLOR(color)

#pragma once

// The shortcuts file (Project64.sc3) stores Windows virtual key codes, so the
// key table keeps them; Qt needs its own key codes.
//   QtKeyFromVirtualKey: 0 when the key has no Qt equivalent
//   VirtualKeyFromQtKey: 0 when the key has no virtual key code
int QtKeyFromVirtualKey(int VirtualKey);
int VirtualKeyFromQtKey(int QtKey);

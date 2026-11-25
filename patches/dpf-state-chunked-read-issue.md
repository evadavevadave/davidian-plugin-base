# Draft upstream report for DISTRHO/DPF

Title: CLAP stateLoad / VST3 setState: parameters lost when the host passes state in small reads

---

**DPF version:** `4238e1c7` (main); the same code is in `develop` (`15e45fe1`).

**What happens**

When a host passes saved state to `stateLoad` (CLAP) or `setState` (VST3) in small reads,
parameter values can be dropped or restored wrong. clap-validator's
`state-reproducibility-buffered` test fails intermittently on a plugin with a single float
parameter:

```
After reloading the state, these parameter values do not match the previously saved values:
 - Gain (0) - 0.000000 (0.0000) vs -55.998428 (-55.9984)
```

**Cause**

In both loaders (`DistrhoPluginCLAP.cpp` around line 1615, `DistrhoPluginVST3.cpp` around
line 1005), after appending the string at `buffer + i`, the code advances
`i += std::strlen(buffer + i)` and then checks `if (buffer[i] == '\0')` to decide that the
key or value is complete. When the string runs to the end of the current read,
`i == read`, and `buffer[read]` is not part of this read: it is the byte saved in `orig`,
left over from an earlier, longer read (or uninitialized). If that byte is zero, a string
cut by the read boundary is treated as complete, so the rest of it becomes a separate
string and keys and values fall out of step.

**Reproduce**

Load a saved state through a stream whose `read` returns a few bytes at a time and leaves
zeros in the rest of the caller's buffer (allowed: the buffer belongs to the reader up to
`size`). The parameter values differ from loading the same state in one read.

**Fix**

Only accept a terminator inside the bytes just read:

```diff
-                if (buffer[i] == '\0')
+                if (i < read && buffer[i] == '\0')
```

in both files. A string that ends exactly at the end of a read then continues in the next
read, which the existing `key +=` / `value +=` appending already handles. With this change
the state round-trips identically for read sizes of 1 to 13 bytes, and clap-validator's
buffered test passes.

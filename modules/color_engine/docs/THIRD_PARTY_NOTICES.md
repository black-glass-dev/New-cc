# Implementation / tooling provenance

Production module-এ কোনো third-party implementation source, LUT, proprietary asset বা GPL port vendored করা হয়নি। Math equations ও published primaries-এর references `SCIENCE.md`-এ আছে; implementation এই module-এর জন্য original। কোনো proprietary application-এর ফলাফল/feature-parity মাপা হয়নি।

Validation tooling আলাদা `build_color/` output tree-তে:

- CMake3.31.6 official PyPI distribution, development/build tool only; module runtime dependency নয়। Distribution-এর নিজস্ব copyright/license files বজায় থাকে।
- Khronos glslang16.6.0 official tagged source archive থেকে compile করা হয়েছে। Production library এর সঙ্গে link করা হয়নি। Tool build output-এর `tools/licenses/glslang-LICENSE.txt`-এ upstream notice preserved; source rebuild করলে original source tree-তেও notices থাকে।
- GCC12.2 / platform C++ standard library: host build toolchain, not copied source।

`tests/bootstrap_tools.sh` pinned tooling পুনরায় তৈরি করতে পারে। এটি network-dependent development utility; module configure/build-এর automatic dependency/download নয়।

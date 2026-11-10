CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
INCLUDES = -Iengine/color/include -Iengine/session/include -Iengine/graph/include -Iengine/core/include -Iengine/timeline/include -Iengine/commands/include -Iengine/project/include
SOURCES = engine/color/src/color_gpu.cpp engine/color/src/color.cpp engine/session/src/session_registry.cpp engine/session/src/session_coordinator.cpp engine/graph/src/graph.cpp engine/core/src/time.cpp engine/timeline/src/timeline.cpp engine/commands/src/command_history.cpp engine/project/src/project.cpp
.PHONY: test clean
test:
	$(CXX) $(CXXFLAGS) -Iengine/color/include engine/color/src/color.cpp engine/color/src/color_gpu.cpp tests/unit/color_gpu_tests.cpp -o /tmp/owncut_color_gpu_tests
	/tmp/owncut_color_gpu_tests
	$(CXX) $(CXXFLAGS) -pthread $(INCLUDES) $(SOURCES) tests/unit/grade_project_tests.cpp -o /tmp/owncut_grade_project_tests
	/tmp/owncut_grade_project_tests
	$(CXX) $(CXXFLAGS) -Iengine/color/include engine/color/src/color_gpu.cpp engine/color/src/color.cpp tests/unit/color_tests.cpp -o /tmp/owncut_color_tests
	/tmp/owncut_color_tests
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) tests/unit/core_tests.cpp -o /tmp/owncut_core_tests
	/tmp/owncut_core_tests
	$(CXX) $(CXXFLAGS) -pthread $(INCLUDES) $(SOURCES) tests/unit/session_tests.cpp -o /tmp/owncut_session_tests
	/tmp/owncut_session_tests
	$(CXX) $(CXXFLAGS) -pthread $(INCLUDES) $(SOURCES) tests/unit/checkpoint_tests.cpp -o /tmp/owncut_checkpoint_tests
	/tmp/owncut_checkpoint_tests
clean:
	rm -f /tmp/owncut_color_gpu_tests /tmp/owncut_grade_project_tests /tmp/owncut_color_tests /tmp/owncut_core_tests /tmp/owncut_session_tests /tmp/owncut_checkpoint_tests

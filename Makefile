CXX = g++
INCLUDES = -I./frontend/include -I./tree/include -I./include -I./backend/include -I./reverse/include -I./middleend/include

CXXFLAGS = -Wall -g -DNDEBUG -Wextra -Wshadow $(INCLUDES)

FRONT_DIR = frontend/src
BACK_DIR = backend/src
TREE_DIR = tree/src
REVERSE_DIR = reverse/src
MIDDLE_DIR = middleend/src
OBJDIR = obj

FRONT_SRCS = $(FRONT_DIR)/frontend_lex.cpp $(FRONT_DIR)/frontend_parse.cpp $(FRONT_DIR)/frontend_main.cpp $(TREE_DIR)/tree.cpp
BACK_SRCS = $(BACK_DIR)/exec_codegen.cpp $(BACK_DIR)/exec_emit.cpp $(BACK_DIR)/exec_backend_main.cpp $(BACK_DIR)/program_symbols.cpp $(BACK_DIR)/elf_write.cpp $(BACK_DIR)/emit_bin.cpp $(BACK_DIR)/runtime_code.cpp $(TREE_DIR)/tree_io.cpp $(TREE_DIR)/tree.cpp
REVERSE_SRC = $(REVERSE_DIR)/reverse_end.cpp $(REVERSE_DIR)/reverse_end_main.cpp $(TREE_DIR)/tree.cpp $(TREE_DIR)/tree_io.cpp
MIDDLE_SRCS = $(MIDDLE_DIR)/middle_end_main.cpp $(MIDDLE_DIR)/middle_end.cpp $(TREE_DIR)/tree.cpp $(TREE_DIR)/tree_io.cpp

FRONT_OBJS = $(addprefix $(OBJDIR)/,$(notdir $(FRONT_SRCS:.cpp=.o)))
BACK_OBJS = $(addprefix $(OBJDIR)/,$(notdir $(BACK_SRCS:.cpp=.o)))
REVERSE_OBJ = $(addprefix $(OBJDIR)/,$(notdir $(REVERSE_SRC:.cpp=.o)))
MIDDLE_OBJS = $(addprefix $(OBJDIR)/,$(notdir $(MIDDLE_SRCS:.cpp=.o)))

all: frontend backend middle_end

frontend: $(FRONT_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o frontend.out

backend: $(BACK_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o backend.out

reverse: $(REVERSE_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o reverse_end.out

middle_end: $(MIDDLE_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o middle.out

vpath %.cpp $(FRONT_DIR) $(BACK_DIR) $(TREE_DIR) $(REVERSE_DIR) $(MIDDLE_DIR)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR)
	rm -f frontend.out backend.out reverse_end.out middle.out

remake: clean all

.PHONY: all clean remake frontend backend reverse middle_end

set(easymake_srcs
	"src/led.c"
	"src/easymake.c")

idf_component_register(SRCS "${easymake_srcs}"
                       INCLUDE_DIRS "include"
                       )
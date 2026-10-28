ifdef FD_HAS_HOSTED
$(call add-objs,fd_poh_tile fd_poh,fd_discof)
endif

ifdef FD_HAS_HOSTED
$(call make-unit-test,test_poh_full,test_poh_full,fd_ballet fd_tango fd_util)
$(call run-unit-test,test_poh_full)
endif

file(GLOB HEADERS "include/eosio/currency_plugin/*.hpp")
add_library( currency_plugin
             currency_plugin.cpp
             ${HEADERS} )

target_link_libraries( currency_plugin PUBLIC chain_plugin eosio_chain appbase jansson curl)
target_include_directories( currency_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
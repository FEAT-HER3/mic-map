/**
 * @file config_manager.cpp
 * @brief P8 D-02: ConfigManagerImpl + createConfigManager() factory + JSON
 *        load/save body relocated to apps/micmap/src/config_manager_impl.cpp
 *        so micmap_core stays JSON-free (AssertNoJsonInCore 4-root scope).
 *
 *        IConfigManager interface remains in
 *        src/core/include/micmap/core/config_manager.hpp (header is JSON-free).
 *
 *        This translation unit intentionally empty — kept in the source list so
 *        existing build/install rules referencing src/config_manager.cpp do not
 *        regress. Tests that require createConfigManager (test_config_manager)
 *        compile config_manager_impl.cpp directly into the test exe (analogous
 *        to test_tray_balloon_once inlining first_launch_balloon.cpp).
 */

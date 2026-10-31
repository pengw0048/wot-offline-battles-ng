# Included from the pinned BigWorld client/CMakeLists.txt after bwclient exists.
if(NOT TARGET bwclient)
    message(FATAL_ERROR "The standalone application must be linked into bwclient")
endif()

set(WOT_NUMERICAL_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/projectile_motion.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/vehicle_control.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/combat_rules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/vehicle_collision.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/gun_ballistics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/stock_gun_history.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_effect_dx9.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_effect_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_d3d9_render_thread.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/stock_shader_binary.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/stock_effect_binding.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/stock_effect_reflection.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/stock_shader_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/point_velocity.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/wwise_bank.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/vehicle_dynamics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/vehicle_drive.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/vehicle_physics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/vehicle_suspension.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/vehicle_authority.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/stock_ballistics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/stock_trajectory_drawer.cpp")
set(WOT_STOCK_ADAPTER_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/offline_fork_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_data.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_compiled_space_mapping.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_lifecycle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_sounds.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_static_model_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_destructibles.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_destructible_falling_query.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_destructible_falling_solver.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_control_points.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_control_point_radius.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_lights.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_flares.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_water.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_water_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_water_parameters.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_water_ripples.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_flash_bang.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_space_decals.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_dynamic_decals.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_ground_decals.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_decals.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_local_lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_replay_controller.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_replay_file.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tree_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tree_scene_loader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tree_materials.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tree_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tree_lod.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tree_wind.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tps_renderer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tps_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_ground_effects.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_damage_sticker_registration.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_stickers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tps_visual.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_scene_settings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tps_quality.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_tps_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_startup_services.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_resource_content.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_scaleform_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_preferences_path.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_preferences_callbacks.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_numeric_format.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_graphics_initialization.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_graphics_settings_consumers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_graphics_presets.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_graphics_settings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_graphics_detection.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_cpu_detection.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_gpu_detection.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_display_modes.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_math.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_math_providers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_math_oscillators.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_wot_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battleground_elements.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_particle_quality.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_environment_constants.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_deferred_lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_deferred_buffers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_antialiasing.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_antialiasing_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_temporal_antialiasing.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_traces.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_flock_manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_dynamic_resolution.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_decal_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_screenshot_callback.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_xmpp.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_pinger.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/client_session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gun_ballistics_binding.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/offline_vehicle_parameters.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vehicle_appearance.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vehicle_physics_binding.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_scene_collision.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_scene_identity.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_scene_prediction.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_scene_queries.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_offline_query_bridge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_offline_query_skin.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_entity_filter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_turret_filter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_avatar_filter_helper.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_avatar_vector_filter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_audio.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_sound_zones.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_audio_io.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_bigworld_ipc.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_glyph_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_scaleform_localization.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_screener.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_ballistics_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_ballistics_simulator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_timed_warplane_motor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_timed_warplane_motor_math.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_runtime_services.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_debug_queries.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_windows_notifier.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_havok_sniper.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_control_mode_rendering.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_rendering.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_trajectory_drawer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_hangar_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_flash.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_marker_canvas.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_minimap.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_gun_marker_providers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_crosshair.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_indicators.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_light_fx.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_lobby_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_components.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_animators.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_vehicle_animation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_vehicle_animation_math.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_wheels.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_track_nodes.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_spline_tracks.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_audio.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_fashions.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_visuals.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_water_sensor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_terrain_sensor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_vehicle_filter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_cursor_camera.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_camera_collider.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_battle_camera.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_bsp_collision_model.cpp")

# Persistent native gameplay owners and their cold/event Python value boundary.
list(APPEND WOT_STOCK_ADAPTER_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_actor_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_replica.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_replica_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_query.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_query_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_targeting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_targeting_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_transport.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_transport_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_wire_codec.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_wire_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_wire_replay.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_wire_net.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_wire_replica_store.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_admission.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_landing.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_input_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_input_controls.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_input_controls_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_survival.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_survival_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_environment.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_environment_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_projectile.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_projectile_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_projectile_world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_projectile_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_weapon.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_weapon_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_weapon_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_weapon_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_weapon_runtime_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_world_install.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_impact.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_feedback.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_motion.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_legacy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_peers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_peers_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_ram.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_ram_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_contacts.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_roster.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_status.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_status_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_presentation_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_self_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_install.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_publication.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_dispatch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_vehicle_hooks.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_critical.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_critical_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_lifecycle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_events.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_shots.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_diagnostics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_recording.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_turret_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_turret_presentation_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_client_drive_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_armor_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_armor_law.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_artillery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_candidate.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_clock.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_combat.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_combat_mode.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_combat_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_combat_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_contacts.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_decision.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_descriptor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_environment.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_foliage_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_human_history.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_human_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_internal_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_internal_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_motion.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_motion_plan.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_motion_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_navigation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_navigation_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_navigation_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_navigator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_observation_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_parking.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_parking_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_perception.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_physics_host.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_physics_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_physics_world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_planner.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_planning_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_planning_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_prewarm.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_publication.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_publication_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_resource_lifecycle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_services.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_step_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_strategy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_turret_obstacles.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_vehicle_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_weapon.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_weapon_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_weapon_world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_bot_world_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_destructible_backend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_destructible_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_detached_turret_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_detached_turret_launch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_detached_turret_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_detached_turret_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_detached_turret_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_detached_turret_world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_human_ram.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_player_contacts.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_player_contacts_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_player_environment.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_player_environment_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_chord.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_cone.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_cursor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_damage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_damage_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_history.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_owner.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_python.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_random.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_projectile_wire.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_world_events.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_world_pipeline.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_native_world_snapshot.cpp"
)
target_sources(bwclient PRIVATE "${CMAKE_CURRENT_LIST_DIR}/standalone_app.cpp"
    ${WOT_NUMERICAL_SOURCES} ${WOT_STOCK_ADAPTER_SOURCES})
include("${CMAKE_CURRENT_LIST_DIR}/offline_fork.cmake")
target_include_directories(bwclient PRIVATE "${BW_SOURCE_DIR}/client"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include")
target_include_directories(compiled_space PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(moo PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include")
target_include_directories(romp PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include")
target_include_directories(particle PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include")
# Scaleform display jobs post to the D3D9 render thread owner.
target_include_directories(scaleform PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/wwise_sdk.cmake")
target_link_libraries(bwclient WotWwiseSdk)
include("${CMAKE_CURRENT_LIST_DIR}/gloox_sdk.cmake")
target_link_libraries(bwclient WotGloox)
target_sources(bwclient PRIVATE
    "${WOT_WWISE_SDK_ROOT}/samples/SoundEngine/Common/AkFilePackage.cpp"
    "${WOT_WWISE_SDK_ROOT}/samples/SoundEngine/Common/AkFilePackageLUT.cpp"
    "${WOT_WWISE_SDK_ROOT}/samples/SoundEngine/Common/AkFileLocationBase.cpp")
# These sources are appended after the legacy client's PCH source enumeration.
# Keep its C++14/PCH contract intact; the recovered numerical core uses C++17.
if(MSVC)
    # Match the original x86 client's address-space contract. Map loading plus
    # the lobby/battle UI exceeds the default 2 GiB process address space.
    target_link_options(bwclient PRIVATE /LARGEADDRESSAWARE)
    set_source_files_properties(${WOT_NUMERICAL_SOURCES} ${WOT_STOCK_ADAPTER_SOURCES}
        PROPERTIES COMPILE_OPTIONS "/std:c++17;/fp:strict;/arch:SSE2")
    # The recovered flock output lives in the original SDK translation unit.
    # Preserve its observed SSE2 double-to-float step and scalar operation order.
    # Its fast-math PCH encodes conflicting floating-point and instruction flags.
    set_source_files_properties("${BW_SOURCE_DIR}/client/boids_filter.cpp"
        PROPERTIES COMPILE_FLAGS "")
    set_property(SOURCE "${BW_SOURCE_DIR}/client/boids_filter.cpp"
        APPEND PROPERTY COMPILE_OPTIONS "/fp:strict;/arch:SSE2")
    # The legacy engine selects /arch:SSE, which leaves scalar arithmetic on
    # x87. Recovered numerical code is checked against the original SSE2 path.
    # These adapters include the public engine's C++14 STL declarations.
    # Retain that observed surface in their otherwise C++17 translation units.
    set_source_files_properties(
        ${WOT_STOCK_ADAPTER_SOURCES}
        PROPERTIES COMPILE_DEFINITIONS
        "_HAS_AUTO_PTR_ETC=1;_HAS_TR1_NAMESPACE=1;_ENFORCE_MATCHING_ALLOCATORS=0")
    # The bundled Python 2.7 declarations still use register parameters.
    # Keep that legacy-header warning local to the engine adapters.
    set_property(SOURCE
        ${WOT_STOCK_ADAPTER_SOURCES}
        APPEND PROPERTY COMPILE_OPTIONS "/wd5033")
    # The ARIE effect owners include the engine profiler under BW_CLIENT.
    set_property(SOURCE
        "${CMAKE_CURRENT_LIST_DIR}/stock_effect_dx9.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/stock_effect_adapter.cpp"
        APPEND PROPERTY COMPILE_DEFINITIONS
        "_HAS_AUTO_PTR_ETC=1;_HAS_TR1_NAMESPACE=1;_ENFORCE_MATCHING_ALLOCATORS=0")
else()
    set_source_files_properties(${WOT_NUMERICAL_SOURCES} ${WOT_STOCK_ADAPTER_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-std=c++17;-fno-fast-math;-ffp-contract=off")
endif()

add_executable(probe_scaleform_lifecycle
    "${CMAKE_CURRENT_LIST_DIR}/../tests/probe_scaleform_lifecycle.cpp")
target_link_libraries(probe_scaleform_lifecycle PRIVATE wot_sf_gfx)
set_target_properties(probe_scaleform_lifecycle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

add_executable(probe_stock_scaleform_amf3
    "${CMAKE_CURRENT_LIST_DIR}/../tests/stock_scaleform_amf3_sdk_probe.cpp")
target_compile_features(probe_stock_scaleform_amf3 PRIVATE cxx_std_14)
target_link_libraries(probe_stock_scaleform_amf3 PRIVATE
    wot_sf_as3 wot_sf_expat user32 gdi32 advapi32)
set_target_properties(probe_stock_scaleform_amf3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

add_executable(probe_stock_scaleform_bitmap
    "${CMAKE_CURRENT_LIST_DIR}/../tests/stock_scaleform_bitmap_sdk_probe.cpp")
target_compile_features(probe_stock_scaleform_bitmap PRIVATE cxx_std_14)
target_link_libraries(probe_stock_scaleform_bitmap PRIVATE
    wot_sf_as3 wot_sf_expat user32 gdi32 advapi32)
set_target_properties(probe_stock_scaleform_bitmap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

add_executable(probe_stock_battleground_elements
    "${CMAKE_CURRENT_LIST_DIR}/../tests/probe_stock_battleground_elements.cpp")
target_compile_features(probe_stock_battleground_elements PRIVATE cxx_std_14)
if(MSVC)
    target_compile_options(probe_stock_battleground_elements PRIVATE /fp:strict)
endif()
set_target_properties(probe_stock_battleground_elements PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

# Exercise the actual native transport against a loopback-only XMPP peer.
add_executable(probe_stock_xmpp
    "${CMAKE_CURRENT_LIST_DIR}/probe_stock_xmpp.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_xmpp.cpp")
target_include_directories(probe_stock_xmpp PRIVATE
    "${BW_SOURCE_DIR}/client" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(probe_stock_xmpp PRIVATE
    _HAS_AUTO_PTR_ETC=1 _HAS_TR1_NAMESPACE=1 _ENFORCE_MATCHING_ALLOCATORS=0)
target_link_libraries(probe_stock_xmpp pyscript cstdmf WotGloox winmm)
set_target_properties(probe_stock_xmpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

# Use the genuine Mercury request/reply transport against a loopback peer.
add_executable(probe_stock_pinger
    "${CMAKE_CURRENT_LIST_DIR}/probe_stock_pinger.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_pinger.cpp")
target_include_directories(probe_stock_pinger PRIVATE
    "${BW_SOURCE_DIR}/client" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(probe_stock_pinger PRIVATE
    _HAS_AUTO_PTR_ETC=1 _HAS_TR1_NAMESPACE=1 _ENFORCE_MATCHING_ALLOCATORS=0)
target_link_libraries(probe_stock_pinger network pyscript cstdmf winmm)
set_target_properties(probe_stock_pinger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

# Verify the actual Python registry, MatrixProvider conversion and D3DX math.
add_executable(probe_stock_wot_module
    "${CMAKE_CURRENT_LIST_DIR}/probe_stock_wot_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_wot_module.cpp")
target_include_directories(probe_stock_wot_module PRIVATE
    "${BW_SOURCE_DIR}/client" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(probe_stock_wot_module PRIVATE
    _HAS_AUTO_PTR_ETC=1 _HAS_TR1_NAMESPACE=1 _ENFORCE_MATCHING_ALLOCATORS=0)
target_link_libraries(probe_stock_wot_module pyscript script resmgr math cstdmf winmm)
set_target_properties(probe_stock_wot_module PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

# This file/packet foundation has no Python controller export. Cross-check its
# real Mercury cipher and zlib output against independently generated fixtures.
add_executable(probe_stock_replay_file
    "${CMAKE_CURRENT_LIST_DIR}/probe_stock_replay_file.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_replay_file.cpp")
target_include_directories(probe_stock_replay_file PRIVATE
    "${BW_SOURCE_DIR}/client" "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${BW_SOURCE_DIR}/third_party/zip")
target_compile_definitions(probe_stock_replay_file PRIVATE
    _HAS_AUTO_PTR_ETC=1 _HAS_TR1_NAMESPACE=1 _ENFORCE_MATCHING_ALLOCATORS=0)
target_link_libraries(probe_stock_replay_file network cstdmf zip)
set_target_properties(probe_stock_replay_file PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

# Verify private writes and mounted archive immutability in the real resource
# manager before the renderer can compile effects against original packages.
add_executable(probe_resource_write_overlay
    "${CMAKE_CURRENT_LIST_DIR}/probe_resource_write_overlay.cpp")
target_include_directories(probe_resource_write_overlay BEFORE PRIVATE
    "${BW_SOURCE_DIR}/lib/resmgr")
target_compile_definitions(probe_resource_write_overlay PRIVATE
    _HAS_AUTO_PTR_ETC=1 _HAS_TR1_NAMESPACE=1 _ENFORCE_MATCHING_ALLOCATORS=0)
target_link_libraries(probe_resource_write_overlay resmgr cstdmf winmm)
set_target_properties(probe_resource_write_overlay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")

# Exercise the production resource adapter with real BW ZIPs and SDK streams.
# No renderer, SoundEngine, audio device, or original game assets are required.
add_executable(probe_stock_audio_resources
    "${CMAKE_CURRENT_LIST_DIR}/probe_stock_audio_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/stock_audio_io.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/wwise_bank.cpp"
    "${WOT_WWISE_SDK_ROOT}/samples/SoundEngine/Common/AkFileLocationBase.cpp")
target_include_directories(probe_stock_audio_resources BEFORE PRIVATE
    "${BW_SOURCE_DIR}/lib/resmgr"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include")
target_compile_definitions(probe_stock_audio_resources PRIVATE
    _HAS_AUTO_PTR_ETC=1 _HAS_TR1_NAMESPACE=1 _ENFORCE_MATCHING_ALLOCATORS=0)
target_link_libraries(probe_stock_audio_resources resmgr cstdmf WotWwiseSdk winmm)
set_target_properties(probe_stock_audio_resources PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native-probes")
if(MSVC)
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/probe_stock_audio_resources.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/probe_resource_write_overlay.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/probe_stock_xmpp.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/probe_stock_pinger.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/probe_stock_wot_module.cpp"
        PROPERTIES COMPILE_OPTIONS "/std:c++17;/fp:strict")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/probe_stock_xmpp.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/probe_stock_pinger.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/probe_stock_wot_module.cpp"
        APPEND PROPERTY COMPILE_OPTIONS "/wd5033")
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/probe_stock_replay_file.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/stock_replay_file.cpp"
        PROPERTIES COMPILE_OPTIONS "/std:c++17;/fp:strict;/wd5033")
endif()

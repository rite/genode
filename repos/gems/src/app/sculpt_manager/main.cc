/*
 * \brief  Sculpt system manager
 * \author Norman Feske
 * \date   2018-04-30
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <os/reporter.h>
#include <nitpicker_session/connection.h>

/* included from depot_deploy tool */
#include <children.h>

/* local includes */
#include <model/child_exit_state.h>
#include <gui.h>
#include <nitpicker.h>
#include <runtime.h>
#include <keyboard_focus.h>
#include <network.h>
#include <storage.h>

namespace Sculpt_manager { struct Main; }


struct Sculpt_manager::Main : Input_event_handler,
                              Dialog::Generator,
                              Runtime_config_generator,
                              Runtime_info,
                              Storage::Target_user
{
	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Nitpicker::Connection _nitpicker { _env, "input" };

	Signal_handler<Main> _input_handler {
		_env.ep(), *this, &Main::_handle_input };

	void _handle_input()
	{
		_nitpicker.input()->for_each_event([&] (Input::Event const &ev) {
			handle_input_event(ev); });
	}

	Expanding_reporter _fonts_config { _env, "config", "fonts_config" };

	Signal_handler<Main> _nitpicker_mode_handler {
		_env.ep(), *this, &Main::_handle_nitpicker_mode };

	void _handle_nitpicker_mode();

	Attached_rom_dataspace _nitpicker_hover { _env, "nitpicker_hover" };

	Signal_handler<Main> _nitpicker_hover_handler {
		_env.ep(), *this, &Main::_handle_nitpicker_hover };

	void _handle_nitpicker_hover();


	/***************************
	 ** Configuration loading **
	 ***************************/

	Prepare_version _prepare_version   { 0 };
	Prepare_version _prepare_completed { 0 };

	bool _prepare_in_progress() const
	{
		return _prepare_version.value != _prepare_completed.value;
	}


	Storage _storage { _env, _heap, *this, *this, *this };

	/**
	 * Storage::Target_user interface
	 */
	void use_storage_target(Storage_target const &target) override
	{
		_storage._sculpt_partition = target;

		/* trigger loading of the configuration from the sculpt partition */
		_prepare_version.value++;

		/* ignore stale query results */
		_query_version.value++;

		_children.apply_config(Xml_node("<config/>"));

		generate_runtime_config();
	}


	Network _network { _env, _heap, *this, *this, *this };


	/************
	 ** Update **
	 ************/

	Attached_rom_dataspace _update_state_rom {
		_env, "report -> runtime/update/state" };

	void _handle_update_state();

	Signal_handler<Main> _update_state_handler {
		_env.ep(), *this, &Main::_handle_update_state };

	Expanding_reporter _installation_reporter { _env, "installation", "installation"};


	/************
	 ** Deploy **
	 ************/

	typedef String<16> Arch;
	Arch _arch { };

	struct Query_version { unsigned value; } _query_version { 0 };

	struct Depot_rom_state { Ram_quota ram_quota; };

	Depot_rom_state _depot_rom_state { 32*1024*1024 };

	Attached_rom_dataspace _manual_deploy_rom { _env, "config -> deploy" };

	Attached_rom_dataspace _blueprint_rom { _env, "report -> runtime/depot_query/blueprint" };

	Expanding_reporter _depot_query_reporter { _env, "query", "depot_query"};

	Depot_deploy::Children _children { _heap };

	void _handle_deploy();

	void _handle_manual_deploy()
	{
		_manual_deploy_rom.update();
		_query_version.value++;
		_handle_deploy();
	}

	void _handle_blueprint()
	{
		_blueprint_rom.update();
		_handle_deploy();
	}

	Signal_handler<Main> _manual_deploy_handler {
		_env.ep(), *this, &Main::_handle_manual_deploy };

	Signal_handler<Main> _blueprint_handler {
		_env.ep(), *this, &Main::_handle_blueprint };


	/************
	 ** Global **
	 ************/

	Gui _gui { _env };

	Expanding_reporter _dialog_reporter { _env, "dialog", "storage_dialog" };

	Attached_rom_dataspace _hover_rom { _env, "storage_view_hover" };

	Signal_handler<Main> _hover_handler {
		_env.ep(), *this, &Main::_handle_hover };

	struct Hovered { enum Dialog { NONE, STORAGE, NETWORK } value; };

	Hovered::Dialog _hovered_dialog { Hovered::NONE };

	template <typename FN>
	void _apply_to_hovered_dialog(Hovered::Dialog dialog, FN const &fn)
	{
		if (dialog == Hovered::STORAGE) fn(_storage.dialog);
		if (dialog == Hovered::NETWORK) fn(_network.dialog);
	}

	void _handle_hover();

	void _gen_download_status(Xml_generator &, Xml_node update_state) const;

	/**
	 * Dialog::Generator interface
	 */
	void generate_dialog() override
	{
		_dialog_reporter.generate([&] (Xml_generator &xml) {

			xml.node("vbox", [&] () {
				gen_named_node(xml, "frame", "logo", [&] () {
					xml.node("float", [&] () {
						xml.node("frame", [&] () {
							xml.attribute("style", "logo"); }); }); });

				_storage.dialog.generate(xml);
				_network.dialog.generate(xml);

				gen_named_node(xml, "frame", "runtime", [&] () {
					xml.node("vbox", [&] () {
						gen_named_node(xml, "label", "title", [&] () {
							xml.attribute("text", "Runtime");
							xml.attribute("font", "title/regular");
						});

						Xml_node state = _update_state_rom.xml();
						if (state.has_sub_node("archive"))
							_gen_download_status(xml, state);
					});
				});
			});
		});
	}

	Attached_rom_dataspace _runtime_state { _env, "report -> runtime/state" };

	/**
	 * Runtime_info interface
	 */
	bool present_in_runtime(Start_name const &name) const override
	{
		bool present = false;
		_runtime_state.xml().for_each_sub_node("child", [&] (Xml_node child) {
			if (child.attribute_value("name", Start_name()) == name)
				present = true; });
		return present;
	}

	Expanding_reporter _runtime_config { _env, "config", "runtime_config" };

	void _generate_runtime_config(Xml_generator &) const;

	/**
	 * Runtime_config_generator interface
	 */
	void generate_runtime_config() override
	{
		_runtime_config.generate([&] (Xml_generator &xml) {
			_generate_runtime_config(xml); });
	}

	Signal_handler<Main> _runtime_state_handler {
		_env.ep(), *this, &Main::_handle_runtime_state };

	void _handle_runtime_state();

	Keyboard_focus _keyboard_focus { _env, _network.dialog, _network.wpa_passphrase };

	/**
	 * Input_event_handler interface
	 */
	void handle_input_event(Input::Event const &ev) override
	{
		if (ev.key_press(Input::BTN_LEFT)) {
			if (_hovered_dialog == Hovered::STORAGE) _storage.dialog.click(_storage);
			if (_hovered_dialog == Hovered::NETWORK) _network.dialog.click(_network);
		}

		if (ev.key_release(Input::BTN_LEFT))
			_storage.dialog.clack(_storage);

		if (_keyboard_focus.target == Keyboard_focus::WPA_PASSPHRASE)
			ev.handle_press([&] (Input::Keycode, Codepoint code) {
				_network.handle_key_press(code); });

		if (ev.press())
			_keyboard_focus.update();
	}

	Nitpicker::Root _gui_nitpicker { _env, _heap, *this };

	Main(Env &env) : _env(env)
	{
		_runtime_state.sigh(_runtime_state_handler);

		_nitpicker.input()->sigh(_input_handler);
		_nitpicker.mode_sigh(_nitpicker_mode_handler);

		/*
		 * Adjust GUI parameters to initial nitpicker mode
		 */
		_handle_nitpicker_mode();

		/*
		 * Subscribe to reports
		 */
		_update_state_rom     .sigh(_update_state_handler);
		_manual_deploy_rom    .sigh(_manual_deploy_handler);
		_blueprint_rom        .sigh(_blueprint_handler);
		_nitpicker_hover      .sigh(_nitpicker_hover_handler);
		_hover_rom            .sigh(_hover_handler);

		/*
		 * Generate initial configurations
		 */
		_network.wifi_disconnect();

		/*
		 * Import initial report content
		 */
		_handle_nitpicker_hover();
		_handle_deploy();
		_storage._handle_storage_devices_update();
		_network._handle_manual_nic_router_config();

		generate_runtime_config();
		generate_dialog();
		_gui.generate_config();
	}
};


void Sculpt_manager::Main::_gen_download_status(Xml_generator &xml, Xml_node state) const
{
	gen_named_node(xml, "frame", "downloads", [&] () {
		xml.node("vbox", [&] () {

			xml.node("label", [&] () {
				xml.attribute("text", "Download"); });

			unsigned count = 0;
			state.for_each_sub_node("archive", [&] (Xml_node archive) {
				gen_named_node(xml, "hbox", String<10>(count++), [&] () {
					gen_named_node(xml, "float", "left", [&] () {
						xml.attribute("west", "yes");
						typedef String<50> Path;
						xml.node("label", [&] () {
							xml.attribute("text", archive.attribute_value("path", Path()));
							xml.attribute("font", "annotation/regular");
						});
					});

					typedef String<16> Info;

					Info        info  = archive.attribute_value("state", Info());
					float const total = archive.attribute_value("total", 0.0);
					float const now   = archive.attribute_value("now",   0.0);

					if (info == "download") {
						if (total > 0.0)
							info = Info((unsigned)((100*now)/total), "%");
						else
							info = Info("fetch");
					}

					gen_named_node(xml, "float", "right", [&] () {
						xml.attribute("east", "yes");
						xml.node("label", [&] () {
							xml.attribute("text", info);
							xml.attribute("font", "annotation/regular");
						});
					});
				});
			});
		});
	});
}


void Sculpt_manager::Main::_handle_nitpicker_mode()
{
	Framebuffer::Mode const mode = _nitpicker.mode();

	_fonts_config.generate([&] (Xml_generator &xml) {
		xml.node("vfs", [&] () {
			gen_named_node(xml, "rom", "Vera.ttf");
			gen_named_node(xml, "rom", "VeraMono.ttf");
			gen_named_node(xml, "dir", "fonts", [&] () {

				auto gen_ttf_dir = [&] (char const *dir_name,
				                        char const *ttf_path, float size_px) {

					gen_named_node(xml, "dir", dir_name, [&] () {
						gen_named_node(xml, "ttf", "regular", [&] () {
							xml.attribute("path",    ttf_path);
							xml.attribute("size_px", size_px);
							xml.attribute("cache",   "256K");
						});
					});
				};

				float const text_size = (float)mode.height() / 60.0;

				gen_ttf_dir("title",      "/Vera.ttf",     text_size*1.25);
				gen_ttf_dir("text",       "/Vera.ttf",     text_size);
				gen_ttf_dir("annotation", "/Vera.ttf",     text_size*0.8);
				gen_ttf_dir("monospace",  "/VeraMono.ttf", text_size);
			});
		});
		xml.node("default-policy", [&] () { xml.attribute("root", "/fonts"); });
	});

	_gui.version.value++;
	_gui.generate_config();
}


void Sculpt_manager::Main::_handle_hover()
{
	_hover_rom.update();
	Xml_node const hover = _hover_rom.xml();

	Hovered::Dialog const orig_hovered_dialog = _hovered_dialog;

	typedef String<32> Top_level_frame;
	Top_level_frame const top_level_frame =
		query_attribute<Top_level_frame>(hover, "dialog", "vbox", "frame", "name");

	_hovered_dialog = Hovered::NONE;
	if (top_level_frame == "network") _hovered_dialog = Hovered::NETWORK;
	if (top_level_frame == "storage") _hovered_dialog = Hovered::STORAGE;

	if (orig_hovered_dialog != _hovered_dialog)
		_apply_to_hovered_dialog(orig_hovered_dialog, [&] (Dialog &dialog) {
			dialog.hover(Xml_node("<hover/>")); });

	_apply_to_hovered_dialog(_hovered_dialog, [&] (Dialog &dialog) {
		dialog.hover(hover.sub_node("dialog").sub_node("vbox")
		                                     .sub_node("frame")); });
}


void Sculpt_manager::Main::_handle_nitpicker_hover()
{
	if (!_storage._discovery_state.discovery_in_progress())
		return;

	_nitpicker_hover.update();

	if (_nitpicker_hover.xml().attribute_value("active", false) == true)
		_storage._discovery_state.user_intervention = true;
}


void Sculpt_manager::Main::_handle_update_state()
{
	_update_state_rom.update();
	generate_dialog();

	bool const installation_complete =
		!_update_state_rom.xml().has_sub_node("archive");

	if (installation_complete) {
		_children.reset_incomplete();
		_handle_deploy();
	}
}


void Sculpt_manager::Main::_handle_runtime_state()
{
	_runtime_state.update();

	Xml_node state = _runtime_state.xml();

	bool reconfigure_runtime = false;

	/* check for completed storage operations */
	_storage._storage_devices.for_each([&] (Storage_device &device) {

		device.for_each_partition([&] (Partition &partition) {

			Storage_target const target { device.label, partition.number };

			if (partition.check_in_progress) {
				String<64> name(target.label(), ".fsck.ext2");
				Child_exit_state exit_state(state, name);

				if (exit_state.exited) {
					if (exit_state.code != 0)
						error("file-system check failed");
					if (exit_state.code == 0)
						log("file-system check succeeded");

					partition.check_in_progress = 0;
					reconfigure_runtime = true;
					_storage.dialog.reset_operation();
				}
			}

			if (partition.format_in_progress) {
				String<64> name(target.label(), ".mkfs.ext2");
				Child_exit_state exit_state(state, name);

				if (exit_state.exited) {
					if (exit_state.code != 0)
						error("file-system creation failed");

					partition.format_in_progress = false;
					partition.file_system.type = File_system::EXT2;

					if (partition.whole_device())
						device.rediscover();

					reconfigure_runtime = true;
					_storage.dialog.reset_operation();
				}
			}

			/* respond to completion of file-system resize operation */
			if (partition.fs_resize_in_progress) {
				Child_exit_state exit_state(state, Start_name(target.label(), ".resize2fs"));
				if (exit_state.exited) {
					partition.fs_resize_in_progress = false;
					reconfigure_runtime = true;
					device.rediscover();
					_storage.dialog.reset_operation();
				}
			}

		}); /* for each partition */

		/* respond to completion of GPT relabeling */
		if (device.relabel_in_progress()) {
			Child_exit_state exit_state(state, device.relabel_start_name());
			if (exit_state.exited) {
				device.rediscover();
				reconfigure_runtime = true;
				_storage.dialog.reset_operation();
			}
		}

		/* respond to completion of GPT expand */
		if (device.gpt_expand_in_progress()) {
			Child_exit_state exit_state(state, device.expand_start_name());
			if (exit_state.exited) {

				/* kick off resize2fs */
				device.for_each_partition([&] (Partition &partition) {
					if (partition.gpt_expand_in_progress) {
						partition.gpt_expand_in_progress = false;
						partition.fs_resize_in_progress  = true;
					}
				});

				reconfigure_runtime = true;
				_storage.dialog.reset_operation();
			}
		}

	}); /* for each device */

	/* remove prepare subsystem when finished */
	{
		Child_exit_state exit_state(state, "prepare");
		if (exit_state.exited) {
			_prepare_completed = _prepare_version;

			/* trigger deployment */
			_handle_deploy();

			/* trigger update and deploy */
			reconfigure_runtime = true;
		}
	}

	/* upgrade ram_fs quota on demand */
	state.for_each_sub_node("child", [&] (Xml_node child) {

		if (child.attribute_value("name", String<16>()) == "ram_fs"
		 && child.has_sub_node("ram")
		 && child.sub_node("ram").has_attribute("requested")) {

			_storage._ram_fs_state.ram_quota.value *= 2;
			reconfigure_runtime = true;
			generate_dialog();
		}
	});

	/* upgrade depot_rom quota on demand */
	state.for_each_sub_node("child", [&] (Xml_node child) {

		if (child.attribute_value("name", String<16>()) == "depot_rom"
		 && child.has_sub_node("ram")
		 && child.sub_node("ram").has_attribute("requested")) {

			_depot_rom_state.ram_quota.value *= 2;
			reconfigure_runtime = true;
		}
	});

	/*
	 * Re-attempt NIC-router configuration as the uplink may have become
	 * available in the meantime.
	 */
	if (_network._nic_target.nic_router_needed()
	 && !_network._nic_router_config_up_to_date)
		_network._generate_nic_router_config();

	if (reconfigure_runtime)
		generate_runtime_config();
}


void Sculpt_manager::Main::_generate_runtime_config(Xml_generator &xml) const
{
	xml.attribute("verbose", "yes");

	xml.node("report", [&] () {
		xml.attribute("init_ram",   "yes");
		xml.attribute("init_caps",  "yes");
		xml.attribute("child_ram",  "yes");
		xml.attribute("delay_ms",   4*500);
		xml.attribute("buffer",     "64K");
	});

	xml.node("parent-provides", [&] () {
		gen_parent_service<Rom_session>(xml);
		gen_parent_service<Cpu_session>(xml);
		gen_parent_service<Pd_session>(xml);
		gen_parent_service<Rm_session>(xml);
		gen_parent_service<Log_session>(xml);
		gen_parent_service<Timer::Session>(xml);
		gen_parent_service<Report::Session>(xml);
		gen_parent_service<Platform::Session>(xml);
		gen_parent_service<Block::Session>(xml);
		gen_parent_service<Usb::Session>(xml);
		gen_parent_service<::File_system::Session>(xml);
		gen_parent_service<Nitpicker::Session>(xml);
		gen_parent_service<Rtc::Session>(xml);
	});

	_storage.gen_runtime_start_nodes(xml);

	/*
	 * Load configuration and update depot config on the sculpt partition
	 */
	if (_storage._sculpt_partition.valid() && _prepare_in_progress())
		xml.node("start", [&] () {
			gen_prepare_start_content(xml, _prepare_version); });

	if (_storage.any_file_system_inspected())
		gen_file_browser(xml, _storage._storage_devices, _storage._ram_fs_state,
		                 _storage._file_browser_version);

	/*
	 * Spawn chroot instances for accessing '/depot' and '/public'. The
	 * chroot instances implicitly refer to the 'default_fs_rw'.
	 */
	if (_storage._sculpt_partition.valid()) {

		auto chroot = [&] (Start_name const &name, Path const &path, Writeable w) {
			xml.node("start", [&] () {
				gen_chroot_start_content(xml, name, path, w); }); };

		chroot("depot_rw",  "/depot",  WRITEABLE);
		chroot("depot",     "/depot",  READ_ONLY);
		chroot("public_rw", "/public", WRITEABLE);
	}

	_network.gen_runtime_start_nodes(xml);

	/*
	 * Update
	 */
	if (_storage._sculpt_partition.valid() && !_prepare_in_progress() && _network.ready())
		xml.node("start", [&] () {
			gen_update_start_content(xml); });

	/*
	 * Deployment infrastructure
	 */
	if (_storage._sculpt_partition.valid() && !_prepare_in_progress()) {

		xml.node("start", [&] () {
			gen_fs_rom_start_content(xml, "depot_rom", "depot",
			                         _depot_rom_state.ram_quota); });

		xml.node("start", [&] () {
			gen_depot_query_start_content(xml); });

		Xml_node const manual_deploy = _manual_deploy_rom.xml();

		/* insert content of '<static>' node as is */
		if (manual_deploy.has_sub_node("static")) {
			Xml_node static_config = manual_deploy.sub_node("static");
			xml.append(static_config.content_base(), static_config.content_size());
		}

		/* generate start nodes for deployed packages */
		if (manual_deploy.has_sub_node("common_routes"))
			_children.gen_start_nodes(xml, manual_deploy.sub_node("common_routes"),
			                          "depot_rom");
	}
}


void Sculpt_manager::Main::_handle_deploy()
{
	Xml_node const manual_deploy = _manual_deploy_rom.xml();

	/* determine CPU architecture of deployment */
	_arch = manual_deploy.attribute_value("arch", Arch());
	if (!_arch.valid())
		warning("manual deploy config lacks 'arch' attribute");

	try { _children.apply_config(manual_deploy); }
	catch (...) {
		error("spurious exception during deploy update (apply_config)"); }

	/* update query for blueprints of all unconfigured start nodes */
	if (_arch.valid()) {
		_depot_query_reporter.generate([&] (Xml_generator &xml) {
			xml.attribute("arch",    _arch);
			xml.attribute("version", _query_version.value);
			_children.gen_queries(xml);
		});
	}

	/*
	 * Apply blueprint after 'gen_queries'. Otherwise the application of a
	 * stale blueprint may flag children whose pkgs have been installed in the
	 * meanwhile as incomplete, suppressing their respective queries.
	 */
	try {
		Xml_node const blueprint = _blueprint_rom.xml();

		/* apply blueprint, except when stale */
		typedef String<32> Version;
		Version const version = blueprint.attribute_value("version", Version());
		if (version == Version(_query_version.value))
			_children.apply_blueprint(_blueprint_rom.xml());
	}
	catch (...) {
		error("spurious exception during deploy update (apply_blueprint)"); }

	/* feed missing packages to installation queue */
	_installation_reporter.generate([&] (Xml_generator &xml) {
		xml.attribute("arch", _arch);
		_children.gen_installation_entries(xml); });

	generate_runtime_config();
}


void Component::construct(Genode::Env &env)
{
	static Sculpt_manager::Main main(env);
}


/*
 * \brief  Network management dialog
 * \author Norman Feske
 * \date   2018-05-07
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _NETWORK_DIALOG_H_
#define _NETWORK_DIALOG_H_

#include "types.h"
#include "dialog.h"
#include "selectable_item.h"
#include "nic_target.h"
#include "nic_state.h"
#include "wifi_connection.h"

namespace Sculpt_manager { struct Network_dialog; }


struct Sculpt_manager::Network_dialog : Dialog
{
	Env &_env;

	Dialog::Generator &_dialog_generator;

	Nic_target      const &_used_nic;
	Access_points   const &_access_points;
	Wifi_connection const &_wifi_connection;
	Nic_state       const &_nic_state;

	Hoverable_item  _nic_item { };
	Selectable_item _ap_item  { };
	Hoverable_item  _nic_info { };

	bool ap_list_hovered() const { return _used_nic.type == Nic_target::WIFI
		                               && _nic_info.hovered("nic_info"); }

	void _gen_access_point(Xml_generator &, Access_point const &) const;
	void _gen_connected_ap(Xml_generator &) const;
	void _gen_access_point_list(Xml_generator &) const;

	void generate(Xml_generator &) const;

	/**
	 * Dialog interface
	 */
	void hover(Xml_node hover) override;

	struct Action : Interface
	{
		virtual void nic_target(Nic_target const &) = 0;

		virtual void wifi_disconnect() = 0;
	};

	void click(Action &action);

	Network_dialog(Env                   &env,
	               Dialog::Generator     &dialog_generator,
	               Nic_target      const &used_nic,
	               Access_points   const &access_points,
	               Wifi_connection const &wifi_connection,
	               Nic_state       const &nic_state)
	:
		_env(env), _dialog_generator(dialog_generator),
		_used_nic(used_nic), _access_points(access_points),
		_wifi_connection(wifi_connection), _nic_state(nic_state)
	{ }
};

#endif /* _NETWORK_DIALOG_H_ */

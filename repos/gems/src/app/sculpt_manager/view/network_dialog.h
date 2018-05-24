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

#ifndef _VIEW__NETWORK_DIALOG_H_
#define _VIEW__NETWORK_DIALOG_H_

#include <types.h>
#include <model/nic_target.h>
#include <model/nic_state.h>
#include <model/wifi_connection.h>
#include <model/wpa_passphrase.h>
#include <view/dialog.h>
#include <view/selectable_item.h>

namespace Sculpt_manager { struct Network_dialog; }


struct Sculpt_manager::Network_dialog : Dialog
{
	Env &_env;

	Dialog::Generator &_dialog_generator;

	Nic_target           const &_used_nic;
	Access_points        const &_access_points;
	Wifi_connection      const &_wifi_connection;
	Nic_state            const &_nic_state;
	Blind_wpa_passphrase const &_wpa_passphrase;

	Hoverable_item  _nic_item     { };
	Selectable_item _ap_item      { };
	Hoverable_item  _nic_info     { };
	Hoverable_item  _connect_item { }; /* confirm WPA passphrase */

	bool ap_list_hovered() const { return _used_nic.type == Nic_target::WIFI
	                                   && _nic_info.hovered("nic_info"); }

	/*
	 * \return true if at least one access point fulfils the condition 'COND_FN'
	 */
	template <typename COND_FN>
	bool _for_each_ap(COND_FN const &cond_fn) const
	{
		bool result = false;
		_access_points.for_each([&] (Access_point const &ap) {
			result |= cond_fn(ap); });
		return result;
	}

	Access_point::Bssid selected_ap() const { return _ap_item._selected; }

	/* limit view to highest-quality access points */
	unsigned const _max_visible_aps = 20;

	/* determine whether the selected AP is present in access-point list */
	bool _selected_ap_visible() const;

	bool _selected_ap_unprotected() const;

	void _gen_access_point(Xml_generator &, Access_point const &) const;
	void _gen_connected_ap(Xml_generator &) const;
	void _gen_access_point_list(Xml_generator &) const;

	void generate(Xml_generator &) const;

	bool need_keyboard_focus_for_passphrase() const;

	/**
	 * Dialog interface
	 */
	void hover(Xml_node hover) override;

	struct Action : Interface
	{
		virtual void nic_target(Nic_target const &) = 0;

		virtual void wifi_connect(Access_point::Ssid) = 0;

		virtual void wifi_disconnect() = 0;
	};

	void click(Action &action);

	Network_dialog(Env                        &env,
	               Dialog::Generator          &dialog_generator,
	               Nic_target           const &used_nic,
	               Access_points        const &access_points,
	               Wifi_connection      const &wifi_connection,
	               Nic_state            const &nic_state,
	               Blind_wpa_passphrase const &wpa_passphrase)
	:
		_env(env), _dialog_generator(dialog_generator),
		_used_nic(used_nic), _access_points(access_points),
		_wifi_connection(wifi_connection), _nic_state(nic_state),
		_wpa_passphrase(wpa_passphrase)
	{ }
};

#endif /* _VIEW__NETWORK_DIALOG_H_ */

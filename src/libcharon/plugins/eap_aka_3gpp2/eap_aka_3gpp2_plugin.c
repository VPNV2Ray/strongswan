/*
 * Copyright (C) 2008-2009 Martin Willi
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "eap_aka_3gpp2_plugin.h"
#include "eap_aka_3gpp2_card.h"
#include "eap_aka_3gpp2_provider.h"
#include "eap_aka_3gpp2_functions.h"

#include <daemon.h>

typedef struct private_eap_aka_3gpp2_t private_eap_aka_3gpp2_t;

/**
 * Private data of an eap_aka_3gpp2_t object.
 */
struct private_eap_aka_3gpp2_t {

	/**
	 * Public eap_aka_3gpp2_plugin_t interface.
	 */
	eap_aka_3gpp2_plugin_t public;

	/**
	 * SIM card
	 */
	eap_aka_3gpp2_card_t *card;

	/**
	 * SIM provider
	 */
	eap_aka_3gpp2_provider_t *provider;

	/**
	 * AKA functions
	 */
	eap_aka_3gpp2_functions_t *functions;
};

METHOD(plugin_t, get_name, char*,
	private_eap_aka_3gpp2_t *this)
{
	return "eap-aka-3gpp2";
}

METHOD(plugin_t, destroy, void,
	private_eap_aka_3gpp2_t *this)
{
	simaka_manager_t *mgr;

	mgr = lib->get(lib, "aka-manager");
	if (mgr)
	{
		mgr->remove_card(mgr, &this->card->card);
		mgr->remove_provider(mgr, &this->provider->provider);
	}
	this->card->destroy(this->card);
	this->provider->destroy(this->provider);
	this->functions->destroy(this->functions);
	free(this);
}

/**
 * See header
 */
plugin_t *eap_aka_3gpp2_plugin_create()
{
	private_eap_aka_3gpp2_t *this;
	simaka_manager_t *mgr;

	INIT(this,
		.public = {
			.plugin = {
				.get_name = _get_name,
				.reload = (void*)return_false,
				.destroy = _destroy,
			},
		},
		.functions = eap_aka_3gpp2_functions_create(),
	);

	if (!this->functions)
	{
		free(this);
		return NULL;
	}
	this->card = eap_aka_3gpp2_card_create(this->functions);
	this->provider = eap_aka_3gpp2_provider_create(this->functions);

	mgr = lib->get(lib, "aka-manager");
	if (mgr)
	{
		mgr->add_card(mgr, &this->card->card);
		mgr->add_provider(mgr, &this->provider->provider);
	}
	return &this->public.plugin;
}


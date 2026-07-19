/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Top-level Qt widget for Sponge DE.
 *
 * Phase 3: a single window that proves Qt rendering works on top of
 * Genode's nitpicker. The widget displays a title and a placeholder
 * label. Panel, launcher, notifications, and theme application are
 * stubbed with Genode::warning per AGENTS.md §5.3 and arrive in
 * later phases.
 */

#pragma once

#include <base/component.h>

#include <QWidget>

namespace Sponge::Sponge_DE {

class Main : public QWidget
{
	Q_OBJECT

	public:

		explicit Main(Genode::Env &env, QWidget *parent = nullptr);

	private:

		Genode::Env &_env;
};

}  /* namespace Sponge::Sponge_DE */

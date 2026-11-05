/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "swdllapi.h"

#include <rtl/ustring.hxx>

#include <cstddef>
#include <vector>

namespace sw
{
/// Word-style "Simple Markup": while tracked changes are hidden, still paint
/// the change bar next to every line that has a tracked change.
SW_DLLPUBLIC void SetChangeBarsWhenHidden(bool bOn);
SW_DLLPUBLIC bool IsChangeBarsWhenHidden();

/// Word "Show Markup > Specific People": changes of these authors are shown
/// as plain text (no insert/delete colouring), like an unchecked reviewer.
SW_DLLPUBLIC void SetHiddenRedlineAuthors(std::vector<OUString> const& rAuthors);
SW_DLLPUBLIC std::vector<OUString> GetHiddenRedlineAuthors();
bool IsRedlineAuthorHidden(std::size_t nAuthor);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#pragma once

#include "scdllapi.h"
#include "SparklineAttributes.hxx"
#include <tools/Guid.hxx>

namespace sc
{
/** Common properties for a group of sparklines */
class SC_DLLPUBLIC SparklineGroup
{
private:
    SparklineAttributes m_aAttributes;
    tools::Guid m_aGUID;
    // Date axis range as an OOXML reference ("Sheet1!A1:E1"), x14:sparklineGroup/xm:f; empty = none
    OUString m_aDateRange;

public:
    SparklineAttributes& getAttributes() { return m_aAttributes; }
    SparklineAttributes const& getAttributes() const { return m_aAttributes; }

    void setAttributes(SparklineAttributes const& rAttributes) { m_aAttributes = rAttributes; };

    tools::Guid& getID() { return m_aGUID; }

    void setID(tools::Guid const& rGuid) { m_aGUID = rGuid; }

    OUString const& getDateRange() const { return m_aDateRange; }
    void setDateRange(OUString const& rRange) { m_aDateRange = rRange; }

    SparklineGroup();
    SparklineGroup(SparklineGroup const& pOtherSparkline);
    SparklineGroup(SparklineAttributes aSparklineAttributes);

    SparklineGroup& operator=(const SparklineGroup&) = delete;
};

} // end sc

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

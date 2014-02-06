/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2012 Jean-Pierre Charras, jean-pierre.charras@ujf-grenoble.fr
 * Copyright (C) 2012 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 1992-2012 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file class_board_connected_item.cpp
 * @brief BOARD_CONNECTED_ITEM class functions.
 */

#include <fctsys.h>
#include <pcbnew.h>

#include <class_board.h>
#include <class_board_item.h>

BOARD_CONNECTED_ITEM::BOARD_CONNECTED_ITEM( BOARD_ITEM* aParent, KICAD_T idtype ) :
    BOARD_ITEM( aParent, idtype ), m_Subnet( 0 ), m_ZoneSubnet( 0 ),
    m_netinfo( &NETINFO_LIST::ORPHANED )
{
    // The unconnected is set only in case the item belongs to a BOARD
    SetNet( NETINFO_LIST::UNCONNECTED );
}


BOARD_CONNECTED_ITEM::BOARD_CONNECTED_ITEM( const BOARD_CONNECTED_ITEM& aItem ) :
    BOARD_ITEM( aItem ), m_Subnet( aItem.m_Subnet ), m_ZoneSubnet( aItem.m_ZoneSubnet ),
    m_netinfo( aItem.m_netinfo )
{
}


int BOARD_CONNECTED_ITEM::GetNet() const
{
    return m_netinfo->GetNet();
}


void BOARD_CONNECTED_ITEM::SetNet( int aNetCode )
{
    BOARD* board = GetBoard();
    if( board )
    {
        m_netinfo = board->FindNet( aNetCode );

        // The requested net does not exist, mark it as unconnected
        if( m_netinfo == NULL )
            m_netinfo = board->FindNet( NETINFO_LIST::UNCONNECTED );
    }
    else
    {
        // There is no board that contains list of nets, the item is orphaned
        m_netinfo = &NETINFO_LIST::ORPHANED;
    }
}


const wxString& BOARD_CONNECTED_ITEM::GetNetname() const
{
    return m_netinfo->GetNetname();
}


const wxString& BOARD_CONNECTED_ITEM::GetShortNetname() const
{
    return m_netinfo->GetShortNetname();
}


int BOARD_CONNECTED_ITEM::GetClearance( BOARD_CONNECTED_ITEM* aItem ) const
{
    NETCLASS*   myclass  = GetNetClass();

    // DO NOT use wxASSERT, because GetClearance is called inside an OnPaint event
    // and a call to wxASSERT can crash the application.
    if( myclass )
    {
        int myClearance  = myclass->GetClearance();
        // @todo : after GetNetClass() is reliably not returning NULL, remove the
        // tests for if( myclass )

        if( aItem )
        {
            int hisClearance = aItem->GetClearance();
            return std::max( hisClearance, myClearance );
        }

        return myClearance;
    }
    else
    {
#ifdef DEBUG
        wxLogWarning(wxT("BOARD_CONNECTED_ITEM::GetClearance():NULL netclass,type %d"), Type() );
#endif
    }

    return 0;
}


NETCLASS* BOARD_CONNECTED_ITEM::GetNetClass() const
{
    // It is important that this be implemented without any sequential searching.
    // Simple array lookups should be fine, performance-wise.
    BOARD*  board = GetBoard();

    // DO NOT use wxASSERT, because GetNetClass is called inside an OnPaint event
    // and a call to wxASSERT can crash the application.

    if( board == NULL )     // Should not occur
    {
#ifdef DEBUG
        wxLogWarning( wxT("BOARD_CONNECTED_ITEM::GetNetClass():NULL board,type %d"), Type() );
#endif
        return NULL;
    }

    NETCLASS*       netclass = NULL;
    int             netcode  = GetNet();
    NETINFO_ITEM*   net = board->FindNet( netcode );

    if( net )
    {
        netclass = net->GetNetClass();

#ifdef DEBUG
        if( netclass == NULL )
        {
            wxLogWarning( wxT("BOARD_CONNECTED_ITEM::GetNetClass():NULL netclass,type %d"), Type() );
        }
#endif
    }

    if( netclass )
        return netclass;
    else
        return board->m_NetClasses.GetDefault();
}


wxString BOARD_CONNECTED_ITEM::GetNetClassName() const
{
    wxString    name;
    NETCLASS*   myclass = GetNetClass();

    if( myclass )
        name = myclass->GetName();
    else
    {
        BOARD*  board = GetBoard();
        name = board->m_NetClasses.GetDefault()->GetName();
    }

    return name;
}

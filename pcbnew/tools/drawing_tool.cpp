/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014-2017 CERN
 * Copyright (C) 2016 KiCad Developers, see AUTHORS.txt for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
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

#include "drawing_tool.h"
#include "pcb_actions.h"

#include <wxPcbStruct.h>
#include <class_draw_panel_gal.h>
#include <project.h>
#include <id.h>
#include <pcbnew_id.h>
#include <confirm.h>
#include <dialog_edit_module_text.h>
#include <import_dxf/dialog_dxf_import.h>

#include <view/view_group.h>
#include <view/view_controls.h>
#include <view/view.h>
#include <gal/graphics_abstraction_layer.h>
#include <tool/tool_manager.h>
#include <router/direction.h>
#include <ratsnest_data.h>
#include <board_commit.h>
#include <scoped_set_reset.h>
#include <bitmaps.h>
#include <hotkeys.h>

#include <class_board.h>
#include <class_edge_mod.h>
#include <class_pcb_text.h>
#include <class_dimension.h>
#include <class_zone.h>
#include <class_module.h>

#include <tools/selection_tool.h>
#include <tools/tool_event_utils.h>

using SCOPED_DRAW_MODE = SCOPED_SET_RESET<DRAWING_TOOL::MODE>;

// Drawing tool actions
TOOL_ACTION PCB_ACTIONS::drawLine( "pcbnew.InteractiveDrawing.line",
        AS_GLOBAL, 0,
        _( "Draw Line" ), _( "Draw a line" ), NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::drawCircle( "pcbnew.InteractiveDrawing.circle",
        AS_GLOBAL, 0,
        _( "Draw Circle" ), _( "Draw a circle" ), NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::drawArc( "pcbnew.InteractiveDrawing.arc",
        AS_GLOBAL, 0,
        _( "Draw Arc" ), _( "Draw an arc" ), NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::placeText( "pcbnew.InteractiveDrawing.text",
        AS_GLOBAL, 0,
        _( "Add Text" ), _( "Add a text" ), NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::drawDimension( "pcbnew.InteractiveDrawing.dimension",
        AS_GLOBAL, 0,
        _( "Add Dimension" ), _( "Add a dimension" ), NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::drawZone( "pcbnew.InteractiveDrawing.zone",
        AS_GLOBAL, 0,
        _( "Add Filled Zone" ), _( "Add a filled zone" ), NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::drawKeepout( "pcbnew.InteractiveDrawing.keepout",
        AS_GLOBAL, 0,
        _( "Add Keepout Area" ), _( "Add a keepout area" ), NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::drawZoneCutout( "pcbnew.InteractiveDrawing.zoneCutout",
        AS_GLOBAL, 0,
        _( "Add a Zone Cutout" ), _( "Add a cutout area of an existing zone" ),
        add_zone_cutout_xpm, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::drawSimilarZone( "pcbnew.InteractiveDrawing.similarZone",
        AS_GLOBAL, 0,
        _( "Add a Similar Zone" ), _( "Add a zone with the same settings as an existing zone" ),
        add_zone_xpm, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::placeDXF( "pcbnew.InteractiveDrawing.placeDXF",
        AS_GLOBAL, 0,
        "Place DXF", "", NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::setAnchor( "pcbnew.InteractiveDrawing.setAnchor",
        AS_GLOBAL, 0,
        _( "Place the Footprint Anchor" ), _( "Place the footprint anchor" ),
        NULL, AF_ACTIVATE );

TOOL_ACTION PCB_ACTIONS::incWidth( "pcbnew.InteractiveDrawing.incWidth",
        AS_CONTEXT, '+',
        _( "Increase Line Width" ), _( "Increase the line width" ) );

TOOL_ACTION PCB_ACTIONS::decWidth( "pcbnew.InteractiveDrawing.decWidth",
        AS_CONTEXT, '-',
        _( "Decrease Line Width" ), _( "Decrease the line width" ) );

TOOL_ACTION PCB_ACTIONS::arcPosture( "pcbnew.InteractiveDrawing.arcPosture",
        AS_CONTEXT, TOOL_ACTION::LegacyHotKey( HK_SWITCH_TRACK_POSTURE ),
        _( "Switch Arc Posture" ), _( "Switch the arc posture" ) );

DRAWING_TOOL::DRAWING_TOOL() :
    PCB_TOOL( "pcbnew.InteractiveDrawing" ),
    m_view( nullptr ), m_controls( nullptr ),
    m_board( nullptr ), m_frame( nullptr ), m_mode( MODE::NONE ),
    m_lineWidth( 1 ),
    m_menu( *this )
{
}


DRAWING_TOOL::~DRAWING_TOOL()
{
}


bool DRAWING_TOOL::Init()
{
    auto activeToolFunctor = [ this ] ( const SELECTION& aSel ) {
        return m_mode != MODE::NONE;
    };

    auto& ctxMenu = m_menu.GetMenu();

    // cancel current toool goes in main context menu at the top if present
    ctxMenu.AddItem( ACTIONS::cancelInteractive, activeToolFunctor, 1000 );
    ctxMenu.AddSeparator( activeToolFunctor, 1000 );

    // Drawing type-specific options will be added by the PCB control tool
    m_menu.AddStandardSubMenus( *getEditFrame<PCB_BASE_FRAME>() );

    return true;
}


void DRAWING_TOOL::Reset( RESET_REASON aReason )
{
    // Init variables used by every drawing tool
    m_view = getView();
    m_controls = getViewControls();
    m_board = getModel<BOARD>();
    m_frame = getEditFrame<PCB_BASE_EDIT_FRAME>();
}


DRAWING_TOOL::MODE DRAWING_TOOL::GetDrawingMode() const
{
    return m_mode;
}


int DRAWING_TOOL::DrawLine( const TOOL_EVENT& aEvent )
{
    BOARD_ITEM_CONTAINER* parent = m_frame->GetModel();
    DRAWSEGMENT* line = m_editModules ? new EDGE_MODULE( (MODULE*) parent ) : new DRAWSEGMENT;
    boost::optional<VECTOR2D> startingPoint;
    BOARD_COMMIT commit( m_frame );

    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::LINE );

    m_frame->SetToolID( m_editModules ? ID_MODEDIT_LINE_TOOL : ID_PCB_ADD_LINE_BUTT,
                        wxCURSOR_PENCIL, _( "Add graphic line" ) );
    m_lineWidth = getSegmentWidth( getDrawingLayer() );

    while( drawSegment( S_SEGMENT, line, startingPoint ) )
    {
        if( line )
        {
            commit.Add( line );
            commit.Push( _( "Draw a line segment" ) );
            startingPoint = line->GetEnd();
        }
        else
        {
            startingPoint = boost::none;
        }

        line = m_editModules ? new EDGE_MODULE( (MODULE*) parent ) : new DRAWSEGMENT;
    }

    m_frame->SetToolID( ID_NO_TOOL_SELECTED, wxCURSOR_DEFAULT, wxEmptyString );

    return 0;
}


int DRAWING_TOOL::DrawCircle( const TOOL_EVENT& aEvent )
{
    BOARD_ITEM_CONTAINER* parent = m_frame->GetModel();
    DRAWSEGMENT* circle = m_editModules ? new EDGE_MODULE( (MODULE*) parent ) : new DRAWSEGMENT;
    BOARD_COMMIT commit( m_frame );

    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::CIRCLE );

    m_frame->SetToolID( m_editModules ? ID_MODEDIT_CIRCLE_TOOL : ID_PCB_CIRCLE_BUTT,
            wxCURSOR_PENCIL, _( "Add graphic circle" ) );
    m_lineWidth = getSegmentWidth( getDrawingLayer() );

    while( drawSegment( S_CIRCLE, circle ) )
    {
        if( circle )
        {
            commit.Add( circle );
            commit.Push( _( "Draw a circle" ) );
        }

        circle = m_editModules ? new EDGE_MODULE( (MODULE*) parent ) : new DRAWSEGMENT;
    }

    m_frame->SetToolID( ID_NO_TOOL_SELECTED, wxCURSOR_DEFAULT, wxEmptyString );

    return 0;
}


int DRAWING_TOOL::DrawArc( const TOOL_EVENT& aEvent )
{
    BOARD_ITEM_CONTAINER* parent = m_frame->GetModel();
    DRAWSEGMENT* arc = m_editModules ? new EDGE_MODULE( (MODULE*) parent ) : new DRAWSEGMENT;
    BOARD_COMMIT commit( m_frame );

    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::ARC );

    m_frame->SetToolID( m_editModules ? ID_MODEDIT_ARC_TOOL : ID_PCB_ARC_BUTT,
            wxCURSOR_PENCIL, _( "Add graphic arc" ) );
    m_lineWidth = getSegmentWidth( getDrawingLayer() );

    while( drawArc( arc ) )
    {
        if( arc )
        {
            commit.Add( arc );
            commit.Push( _( "Draw an arc" ) );
        }

        arc = m_editModules ? new EDGE_MODULE( (MODULE*) parent ) : new DRAWSEGMENT;
    }

    m_frame->SetToolID( ID_NO_TOOL_SELECTED, wxCURSOR_DEFAULT, wxEmptyString );

    return 0;
}


int DRAWING_TOOL::PlaceText( const TOOL_EVENT& aEvent )
{
    BOARD_ITEM* text = NULL;
    const BOARD_DESIGN_SETTINGS& dsnSettings = m_frame->GetDesignSettings();
    BOARD_COMMIT commit( m_frame );

    // Add a VIEW_GROUP that serves as a preview for the new item
    SELECTION preview;
    m_view->Add( &preview );

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_controls->ShowCursor( true );
    m_controls->SetSnapping( true );
    // do not capture or auto-pan until we start placing some text

    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::TEXT );

    Activate();
    m_frame->SetToolID( m_editModules ? ID_MODEDIT_TEXT_TOOL : ID_PCB_ADD_TEXT_BUTT,
                        wxCURSOR_PENCIL, _( "Add text" ) );

    // Main loop: keep receiving events
    while( OPT_TOOL_EVENT evt = Wait() )
    {
        VECTOR2I cursorPos = m_controls->GetCursorPosition();

        if( TOOL_EVT_UTILS::IsCancelInteractive( *evt ) )
        {
            if( text )
            {
                // Delete the old text and have another try
                delete text;
                text = NULL;

                preview.Clear();

                m_controls->SetAutoPan( false );
                m_controls->CaptureCursor( false );
                m_controls->ShowCursor( true );
            }
            else
                break;

            if( evt->IsActivate() )  // now finish unconditionally
                break;
        }

        else if( text && evt->Category() == TC_COMMAND )
        {
            if( TOOL_EVT_UTILS::IsRotateToolEvt( *evt ) )
            {
                const auto rotationAngle = TOOL_EVT_UTILS::GetEventRotationAngle(
                        *m_frame, *evt );

                text->Rotate( text->GetPosition(), rotationAngle );
                m_view->Update( &preview );
            }
            else if( evt->IsAction( &PCB_ACTIONS::flip ) )
            {
                text->Flip( text->GetPosition() );
                m_view->Update( &preview );
            }
        }

        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu();
        }

        else if( evt->IsClick( BUT_LEFT ) )
        {
            if( !text )
            {
                // Init the new item attributes
                if( m_editModules )
                {
                    TEXTE_MODULE* textMod = new TEXTE_MODULE( (MODULE*) m_frame->GetModel() );

                    textMod->SetLayer( m_frame->GetActiveLayer() );
                    textMod->SetTextSize( dsnSettings.m_ModuleTextSize );
                    textMod->SetThickness( dsnSettings.m_ModuleTextWidth );
                    textMod->SetTextPos( wxPoint( cursorPos.x, cursorPos.y ) );

                    DialogEditModuleText textDialog( m_frame, textMod, NULL );
                    bool placing;

                    RunMainStack( [&]() {
                        placing = textDialog.ShowModal() && ( textMod->GetText().Length() > 0 );
                    } );

                    if( placing )
                        text = textMod;
                    else
                        delete textMod;
                }
                else
                {
                    TEXTE_PCB* textPcb = new TEXTE_PCB( m_frame->GetModel() );
                    // TODO we have to set IS_NEW, otherwise InstallTextPCB.. creates an undo entry :| LEGACY_CLEANUP
                    textPcb->SetFlags( IS_NEW );

                    LAYER_ID layer = m_frame->GetActiveLayer();
                    textPcb->SetLayer( layer );

                    // Set the mirrored option for layers on the BACK side of the board
                    if( IsBackLayer( layer ) )
                        textPcb->SetMirrored( true );

                    textPcb->SetTextSize( dsnSettings.m_PcbTextSize );
                    textPcb->SetThickness( dsnSettings.m_PcbTextWidth );
                    textPcb->SetTextPos( wxPoint( cursorPos.x, cursorPos.y ) );

                    RunMainStack( [&]() {
                        getEditFrame<PCB_EDIT_FRAME>()->InstallTextPCBOptionsFrame( textPcb, NULL );
                    } );

                    if( textPcb->GetText().IsEmpty() )
                        delete textPcb;
                    else
                        text = textPcb;
                }

                if( text == NULL )
                    continue;

                m_controls->CaptureCursor( true );
                m_controls->SetAutoPan( true );
                //m_controls->ShowCursor( false );

                preview.Add( text );
            }
            else
            {
                //assert( text->GetText().Length() > 0 );
                //assert( text->GetTextSize().x > 0 && text->GetTextSize().y > 0 );

                text->ClearFlags();
                preview.Remove( text );

                commit.Add( text );
                commit.Push( _( "Place a text" ) );

                m_controls->CaptureCursor( false );
                m_controls->SetAutoPan( false );
                m_controls->ShowCursor( true );

                text = NULL;
            }
        }

        else if( text && evt->IsMotion() )
        {
            text->SetPosition( wxPoint( cursorPos.x, cursorPos.y ) );

            // Show a preview of the item
            m_view->Update( &preview );
        }
    }

    m_controls->ShowCursor( false );
    m_controls->SetSnapping( false );
    m_controls->SetAutoPan( false );
    m_controls->CaptureCursor( false );

    m_view->Remove( &preview );
    m_frame->SetToolID( ID_NO_TOOL_SELECTED, wxCURSOR_DEFAULT, wxEmptyString );

    return 0;

}


int DRAWING_TOOL::DrawDimension( const TOOL_EVENT& aEvent )
{
    DIMENSION* dimension = NULL;
    BOARD_COMMIT commit( m_frame );
    int maxThickness;

    // Add a VIEW_GROUP that serves as a preview for the new item
    SELECTION preview;
    m_view->Add( &preview );

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_controls->ShowCursor( true );
    m_controls->SetSnapping( true );

    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::DIMENSION );

    Activate();
    m_frame->SetToolID( ID_PCB_DIMENSION_BUTT, wxCURSOR_PENCIL, _( "Add dimension" ) );
    m_lineWidth = getSegmentWidth( getDrawingLayer() );

    enum DIMENSION_STEPS
    {
        SET_ORIGIN = 0,
        SET_END,
        SET_HEIGHT,
        FINISHED
    };
    int step = SET_ORIGIN;

    // Main loop: keep receiving events
    while( OPT_TOOL_EVENT evt = Wait() )
    {
        VECTOR2I cursorPos = m_controls->GetCursorPosition();

        if( TOOL_EVT_UTILS::IsCancelInteractive( *evt ) )
        {
            if( step != SET_ORIGIN )    // start from the beginning
            {
                preview.Clear();

                delete dimension;
                step = SET_ORIGIN;
            }
            else
                break;

            if( evt->IsActivate() )  // now finish unconditionally
                break;
        }

        else if( evt->IsAction( &PCB_ACTIONS::incWidth ) && step != SET_ORIGIN )
        {
            m_lineWidth += WIDTH_STEP;
            dimension->SetWidth( m_lineWidth );
            m_view->Update( &preview );
        }

        else if( evt->IsAction( &PCB_ACTIONS::decWidth ) && step != SET_ORIGIN )
        {
            if( m_lineWidth > WIDTH_STEP )
            {
                m_lineWidth -= WIDTH_STEP;
                dimension->SetWidth( m_lineWidth );
                m_view->Update( &preview );
            }
        }

        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu();
        }

        else if( evt->IsClick( BUT_LEFT ) )
        {
            switch( step )
            {
            case SET_ORIGIN:
                {
                    LAYER_ID layer = getDrawingLayer();

                    // Init the new item attributes
                    dimension = new DIMENSION( m_board );
                    dimension->SetLayer( layer );
                    dimension->SetOrigin( wxPoint( cursorPos.x, cursorPos.y ) );
                    dimension->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
                    dimension->Text().SetTextSize( m_board->GetDesignSettings().m_PcbTextSize );

                    int width = m_board->GetDesignSettings().m_PcbTextWidth;
                    maxThickness = Clamp_Text_PenSize( width, dimension->Text().GetTextSize() );

                    if( width > maxThickness )
                        width = maxThickness;

                    dimension->Text().SetThickness( width );
                    dimension->SetWidth( width );
                    dimension->AdjustDimensionDetails();

                    preview.Add( dimension );

                    m_controls->SetAutoPan( true );
                    m_controls->CaptureCursor( true );
                }
                break;

            case SET_END:
                dimension->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );

                // Dimensions that have origin and end in the same spot are not valid
                if( dimension->GetOrigin() == dimension->GetEnd() )
                    --step;
                break;

            case SET_HEIGHT:
                {
                    if( wxPoint( cursorPos.x, cursorPos.y ) != dimension->GetPosition() )
                    {
                        assert( dimension->GetOrigin() != dimension->GetEnd() );
                        assert( dimension->GetWidth() > 0 );

                        preview.Remove( dimension );

                        commit.Add( dimension );
                        commit.Push( _( "Draw a dimension" ) );

                    }
                }
                break;
            }

            if( ++step == FINISHED )
            {
                step = SET_ORIGIN;
                m_controls->SetAutoPan( false );
                m_controls->CaptureCursor( false );
            }
        }

        else if( evt->IsMotion() )
        {
            switch( step )
            {
            case SET_END:
                dimension->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
                break;

            case SET_HEIGHT:
            {
                // Calculating the direction of travel perpendicular to the selected axis
                double angle = dimension->GetAngle() + ( M_PI / 2 );

                wxPoint pos( cursorPos.x, cursorPos.y );
                wxPoint delta( pos - dimension->m_featureLineDO );
                double height  = ( delta.x * cos( angle ) ) + ( delta.y * sin( angle ) );
                dimension->SetHeight( height );
            }
            break;
            }

            // Show a preview of the item
            m_view->Update( &preview );
        }
    }

    if( step != SET_ORIGIN )
        delete dimension;

    m_controls->ShowCursor( false );
    m_controls->SetSnapping( false );
    m_controls->SetAutoPan( false );
    m_controls->CaptureCursor( false );
    m_view->Remove( &preview );

    m_frame->SetToolID( ID_NO_TOOL_SELECTED, wxCURSOR_DEFAULT, wxEmptyString );

    return 0;
}


int DRAWING_TOOL::DrawZone( const TOOL_EVENT& aEvent )
{
    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::ZONE );
    m_frame->SetToolID( ID_PCB_ZONES_BUTT, wxCURSOR_PENCIL, _( "Add zones" ) );

    return drawZone( false, ZONE_MODE::ADD );
}


int DRAWING_TOOL::DrawKeepout( const TOOL_EVENT& aEvent )
{
    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::KEEPOUT );
    m_frame->SetToolID( ID_PCB_KEEPOUT_AREA_BUTT, wxCURSOR_PENCIL, _( "Add keepout" ) );

    return drawZone( true, ZONE_MODE::ADD );
}


int DRAWING_TOOL::DrawZoneCutout( const TOOL_EVENT& aEvent )
{
    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::ZONE );
    m_frame->SetToolID( ID_PCB_KEEPOUT_AREA_BUTT, wxCURSOR_PENCIL, _( "Add zone cutout" ) );

    return drawZone( false, ZONE_MODE::CUTOUT );
}


int DRAWING_TOOL::DrawSimilarZone( const TOOL_EVENT& aEvent )
{
    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::ZONE );
    m_frame->SetToolID( ID_PCB_KEEPOUT_AREA_BUTT, wxCURSOR_PENCIL, _( "Add similar zone" ) );

    return drawZone( false, ZONE_MODE::SIMILAR );
}


int DRAWING_TOOL::PlaceDXF( const TOOL_EVENT& aEvent )
{
    if( !m_frame->GetModel() )
        return 0;

    DIALOG_DXF_IMPORT dlg( m_frame );
    int dlgResult = dlg.ShowModal();

    const std::list<BOARD_ITEM*>& list = dlg.GetImportedItems();

    if( dlgResult != wxID_OK || list.empty() )
        return 0;

    VECTOR2I cursorPos = m_controls->GetCursorPosition();
    VECTOR2I delta = cursorPos - list.front()->GetPosition();

    // Add a VIEW_GROUP that serves as a preview for the new item
    SELECTION preview;
    BOARD_COMMIT commit( m_frame );

    // Build the undo list & add items to the current view
    for( auto item : list )
    {
        assert( item->Type() == PCB_LINE_T || item->Type() == PCB_TEXT_T );
        preview.Add( item );
    }

    BOARD_ITEM* firstItem = static_cast<BOARD_ITEM*>( preview.Front() );
    m_view->Add( &preview );

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_controls->ShowCursor( true );
    m_controls->SetSnapping( true );

    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::DXF );

    Activate();

    // Main loop: keep receiving events
    while( OPT_TOOL_EVENT evt = Wait() )
    {
        cursorPos = m_controls->GetCursorPosition();

        if( evt->IsMotion() )
        {
            delta = cursorPos - firstItem->GetPosition();

            for( auto item : preview )
                static_cast<BOARD_ITEM*>( item )->Move( wxPoint( delta.x, delta.y ) );

            m_view->Update( &preview );
        }

        else if( evt->Category() == TC_COMMAND )
        {
            // TODO it should be handled by EDIT_TOOL, so add items and select?
            if( TOOL_EVT_UTILS::IsRotateToolEvt( *evt ) )
            {
                const auto rotationPoint = wxPoint( cursorPos.x, cursorPos.y );
                const auto rotationAngle = TOOL_EVT_UTILS::GetEventRotationAngle(
                        *m_frame, *evt );

                for( auto item : preview )
                {
                    static_cast<BOARD_ITEM*>( item )->Rotate( rotationPoint, rotationAngle );
                }

                m_view->Update( &preview );
            }
            else if( evt->IsAction( &PCB_ACTIONS::flip ) )
            {
                for( auto item : preview )
                    static_cast<BOARD_ITEM*>( item )->Flip( wxPoint( cursorPos.x, cursorPos.y ) );

                m_view->Update( &preview );
            }
            else if( TOOL_EVT_UTILS::IsCancelInteractive( *evt ) )
            {
                preview.FreeItems();
                break;
            }
        }

        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu();
        }

        else if( evt->IsClick( BUT_LEFT ) )
        {
            // Place the drawing
            BOARD_ITEM_CONTAINER* parent = m_frame->GetModel();

            for( auto item : preview )
            {
                if( m_editModules )
                {
                    // Modules use different types for the same things,
                    // so we need to convert imported items to appropriate classes.
                    BOARD_ITEM* converted = NULL;

                    switch( item->Type() )
                    {
                    case PCB_TEXT_T:
                    {
                        TEXTE_PCB* text = static_cast<TEXTE_PCB*>( item );
                        TEXTE_MODULE* textMod = new TEXTE_MODULE( (MODULE*) parent );

                        // Assignment operator also copies the item PCB_TEXT_T type,
                        // so it cannot be added to a module which handles PCB_MODULE_TEXT_T
                        textMod->SetText( text->GetText() );
#if 0
                        textMod->SetTextSize( text->GetTextSize() );
                        textMod->SetThickness( text->GetThickness() );
                        textMod->SetOrientation( text->GetTextAngle() );
                        textMod->SetTextPos( text->GetTextPos() );
                        textMod->SetTextSize( text->GetTextSize() );
                        textMod->SetVisible( text->GetVisible() );
                        textMod->SetMirrored( text->IsMirrored() );
                        textMod->SetItalic( text->IsItalic() );
                        textMod->SetBold( text->IsBold() );
                        textMod->SetHorizJustify( text->GetHorizJustify() );
                        textMod->SetVertJustify( text->GetVertJustify() );
                        textMod->SetMultilineAllowed( text->IsMultilineAllowed() );
#else
                        textMod->EDA_TEXT::SetEffects( *text );
                        textMod->SetLocalCoord();   // using changed SetTexPos() via SetEffects()
#endif
                        converted = textMod;
                        break;
                    }

                    case PCB_LINE_T:
                    {
                        DRAWSEGMENT* seg = static_cast<DRAWSEGMENT*>( item );
                        EDGE_MODULE* modSeg = new EDGE_MODULE( (MODULE*) parent );

                        // Assignment operator also copies the item PCB_LINE_T type,
                        // so it cannot be added to a module which handles PCB_MODULE_EDGE_T
                        modSeg->SetWidth( seg->GetWidth() );
                        modSeg->SetStart( seg->GetStart() );
                        modSeg->SetEnd( seg->GetEnd() );
                        modSeg->SetAngle( seg->GetAngle() );
                        modSeg->SetShape( seg->GetShape() );
                        modSeg->SetType( seg->GetType() );
                        modSeg->SetBezControl1( seg->GetBezControl1() );
                        modSeg->SetBezControl2( seg->GetBezControl2() );
                        modSeg->SetBezierPoints( seg->GetBezierPoints() );
                        modSeg->SetPolyPoints( seg->GetPolyPoints() );
                        converted = modSeg;
                        break;
                    }

                    default:
                        assert( false );
                        break;
                    }

                    if( converted )
                        converted->SetLayer( static_cast<BOARD_ITEM*>( item )->GetLayer() );

                    delete item;
                    item = converted;
                }

                if( item )
                    commit.Add( item );
            }

            commit.Push( _( "Place a DXF drawing" ) );
            break;
        }
    }

    preview.Clear();

    m_controls->ShowCursor( false );
    m_controls->SetSnapping( false );
    m_controls->SetAutoPan( false );
    m_controls->CaptureCursor( false );
    m_view->Remove( &preview );

    return 0;
}


int DRAWING_TOOL::SetAnchor( const TOOL_EVENT& aEvent )
{
    assert( m_editModules );

    SCOPED_DRAW_MODE scopedDrawMode( m_mode, MODE::ANCHOR );

    Activate();
    m_frame->SetToolID( ID_MODEDIT_ANCHOR_TOOL, wxCURSOR_PENCIL,
                        _( "Place the footprint anchor" ) );

    m_controls->ShowCursor( true );
    m_controls->SetSnapping( true );
    m_controls->SetAutoPan( true );
    m_controls->CaptureCursor( false );

    while( OPT_TOOL_EVENT evt = Wait() )
    {
        if( evt->IsClick( BUT_LEFT ) )
        {
            MODULE* module = (MODULE*) m_frame->GetModel();
            BOARD_COMMIT commit( m_frame );
            commit.Modify( module );

            // set the new relative internal local coordinates of footprint items
            VECTOR2I cursorPos = m_controls->GetCursorPosition();
            wxPoint moveVector = module->GetPosition() - wxPoint( cursorPos.x, cursorPos.y );
            module->MoveAnchorPosition( moveVector );

            commit.Push( _( "Move the footprint reference anchor" ) );

            // Usually, we do not need to change twice the anchor position,
            // so deselect the active tool
            break;
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu();
        }
        else if( TOOL_EVT_UTILS::IsCancelInteractive( *evt )  )
            break;
    }

    m_controls->SetAutoPan( false );
    m_controls->CaptureCursor( false );
    m_controls->SetSnapping( false );
    m_controls->ShowCursor( false );

    m_frame->SetToolID( ID_NO_TOOL_SELECTED, wxCURSOR_DEFAULT, wxEmptyString );

    return 0;
}


bool DRAWING_TOOL::drawSegment( int aShape, DRAWSEGMENT*& aGraphic,
                                boost::optional<VECTOR2D> aStartingPoint )
{
    // Only two shapes are currently supported
    assert( aShape == S_SEGMENT || aShape == S_CIRCLE );

    DRAWSEGMENT line45;

    // Add a VIEW_GROUP that serves as a preview for the new item
    SELECTION preview;
    m_view->Add( &preview );

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_controls->ShowCursor( true );
    m_controls->SetSnapping( true );

    Activate();

    bool direction45 = false;       // 45 degrees only mode
    bool started = false;
    VECTOR2I cursorPos = m_controls->GetCursorPosition();

    if( aStartingPoint )
    {
        // Init the new item attributes
        aGraphic->SetShape( (STROKE_T) aShape );
        aGraphic->SetWidth( m_lineWidth );
        aGraphic->SetStart( wxPoint( aStartingPoint->x, aStartingPoint->y ) );
        aGraphic->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
        aGraphic->SetLayer( getDrawingLayer() );

        if( aShape == S_SEGMENT )
            line45 = *aGraphic; // used only for direction 45 mode with lines

        preview.Add( aGraphic );
        m_controls->SetAutoPan( true );
        m_controls->CaptureCursor( true );

        started = true;
    }

    // Main loop: keep receiving events
    while( OPT_TOOL_EVENT evt = Wait() )
    {
        cursorPos = m_controls->GetCursorPosition();

        // 45 degree angle constraint enabled with an option and toggled with Ctrl
        const bool limit45 = ( g_Segments_45_Only != !!( evt->Modifier( MD_CTRL ) ) );

        if( direction45 != limit45 && started && aShape == S_SEGMENT )
        {
            direction45 = limit45;

            if( direction45 )
            {
                preview.Add( &line45 );
                make45DegLine( aGraphic, &line45 );
            }
            else
            {
                preview.Remove( &line45 );
                aGraphic->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
            }

            m_view->Update( &preview );
        }

        if( TOOL_EVT_UTILS::IsCancelInteractive( *evt ) )
        {
            preview.Clear();
            m_view->Update( &preview );
            delete aGraphic;
            aGraphic = NULL;
            break;
        }
        else if( evt->IsAction( &PCB_ACTIONS::layerChanged ) )
        {
            aGraphic->SetLayer( getDrawingLayer() );
            m_view->Update( &preview );
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu();
        }
        else if( evt->IsClick( BUT_LEFT ) || evt->IsDblClick( BUT_LEFT ) )
        {
            if( !started )
            {
                // Init the new item attributes
                aGraphic->SetShape( (STROKE_T) aShape );
                aGraphic->SetWidth( m_lineWidth );
                aGraphic->SetStart( wxPoint( cursorPos.x, cursorPos.y ) );
                aGraphic->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
                aGraphic->SetLayer( getDrawingLayer() );

                if( aShape == S_SEGMENT )
                    line45 = *aGraphic; // used only for direction 45 mode with lines

                preview.Add( aGraphic );
                m_controls->SetAutoPan( true );
                m_controls->CaptureCursor( true );

                started = true;
            }
            else
            {
                if( aGraphic->GetEnd() == aGraphic->GetStart() ||
                        ( evt->IsDblClick( BUT_LEFT ) && aShape == S_SEGMENT ) )
                                        // User has clicked twice in the same spot
                {                       // a clear sign that the current drawing is finished
                    // Now we have to add the helper line as well
                    if( direction45 )
                    {
                        BOARD_ITEM_CONTAINER* parent = m_frame->GetModel();
                        DRAWSEGMENT* l = m_editModules ? new EDGE_MODULE( (MODULE*) parent )
                                                       : new DRAWSEGMENT;

                        // Copy coordinates, layer, etc.
                        *static_cast<DRAWSEGMENT*>( l ) = line45;
                        l->SetEnd( aGraphic->GetStart() );

                        BOARD_COMMIT commit( m_frame );
                        commit.Add( l );
                        commit.Push( _( "Draw a line" ) );
                    }

                    delete aGraphic;
                    aGraphic = NULL;
                }

                preview.Clear();
                break;
            }
        }

        else if( evt->IsMotion() )
        {
            // 45 degree lines
            if( direction45 && aShape == S_SEGMENT )
                make45DegLine( aGraphic, &line45 );
            else
                aGraphic->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );

            m_view->Update( &preview );
        }

        else if( evt->IsAction( &PCB_ACTIONS::incWidth ) )
        {
            m_lineWidth += WIDTH_STEP;
            aGraphic->SetWidth( m_lineWidth );
            line45.SetWidth( m_lineWidth );
            m_view->Update( &preview );
        }

        else if( evt->IsAction( &PCB_ACTIONS::decWidth ) && ( m_lineWidth > WIDTH_STEP ) )
        {
            m_lineWidth -= WIDTH_STEP;
            aGraphic->SetWidth( m_lineWidth );
            line45.SetWidth( m_lineWidth );
            m_view->Update( &preview );
        }
    }

    m_controls->ShowCursor( false );
    m_controls->SetSnapping( false );
    m_controls->SetAutoPan( false );
    m_controls->CaptureCursor( false );
    m_view->Remove( &preview );

    return started;
}


bool DRAWING_TOOL::drawArc( DRAWSEGMENT*& aGraphic )
{
    bool clockwise = true;      // drawing direction of the arc
    double startAngle = 0.0f;   // angle of the first arc line
    VECTOR2I cursorPos = m_controls->GetCursorPosition();

    // Line from the arc center to its origin, to visualize its radius
    DRAWSEGMENT helperLine;
    helperLine.SetShape( S_SEGMENT );
    helperLine.SetLayer( Dwgs_User );
    helperLine.SetWidth( 1 );

    // Add a VIEW_GROUP that serves as a preview for the new item
    SELECTION preview;
    m_view->Add( &preview );

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_controls->ShowCursor( true );
    m_controls->SetSnapping( true );

    Activate();

    enum ARC_STEPS
    {
        SET_ORIGIN = 0,
        SET_END,
        SET_ANGLE,
        FINISHED
    };
    int step = SET_ORIGIN;

    // Main loop: keep receiving events
    while( OPT_TOOL_EVENT evt = Wait() )
    {
        cursorPos = m_controls->GetCursorPosition();

        if( TOOL_EVT_UTILS::IsCancelInteractive( *evt ) )
        {
            preview.Clear();
            delete aGraphic;
            aGraphic = NULL;
            break;
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu();
        }
        else if( evt->IsClick( BUT_LEFT ) )
        {
            switch( step )
            {
            case SET_ORIGIN:
            {
                LAYER_ID layer = getDrawingLayer();

                if( layer == Edge_Cuts )    // dimensions are not allowed on EdgeCuts
                    layer = Dwgs_User;

                // Init the new item attributes
                aGraphic->SetShape( S_ARC );
                aGraphic->SetAngle( 0.0 );
                aGraphic->SetWidth( m_lineWidth );
                aGraphic->SetCenter( wxPoint( cursorPos.x, cursorPos.y ) );
                aGraphic->SetLayer( layer );

                helperLine.SetStart( aGraphic->GetCenter() );
                helperLine.SetEnd( aGraphic->GetCenter() );

                preview.Add( aGraphic );
                preview.Add( &helperLine );

                m_controls->SetAutoPan( true );
                m_controls->CaptureCursor( true );
            }
            break;

            case SET_END:
            {
                if( wxPoint( cursorPos.x, cursorPos.y ) != aGraphic->GetCenter() )
                {
                    VECTOR2D startLine( aGraphic->GetArcStart() - aGraphic->GetCenter() );
                    startAngle = startLine.Angle();
                    aGraphic->SetArcStart( wxPoint( cursorPos.x, cursorPos.y ) );
                }
                else
                    --step;     // one another chance to draw a proper arc
            }
            break;

            case SET_ANGLE:
            {
                if( wxPoint( cursorPos.x, cursorPos.y ) != aGraphic->GetArcStart() && aGraphic->GetAngle() != 0 )
                {
                    assert( aGraphic->GetArcStart() != aGraphic->GetArcEnd() );
                    assert( aGraphic->GetWidth() > 0 );

                    preview.Remove( aGraphic );
                    preview.Remove( &helperLine );
                }
                else
                    --step;     // one another chance to draw a proper arc
            }
            break;
            }

            if( ++step == FINISHED )
                break;
        }

        else if( evt->IsMotion() )
        {
            switch( step )
            {
            case SET_END:
                helperLine.SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
                aGraphic->SetArcStart( wxPoint( cursorPos.x, cursorPos.y ) );
                break;

            case SET_ANGLE:
            {
                VECTOR2D endLine( wxPoint( cursorPos.x, cursorPos.y ) - aGraphic->GetCenter() );
                double newAngle = RAD2DECIDEG( endLine.Angle() - startAngle );

                // Adjust the new angle to (counter)clockwise setting
                if( clockwise && newAngle < 0.0 )
                    newAngle += 3600.0;
                else if( !clockwise && newAngle > 0.0 )
                    newAngle -= 3600.0;

                aGraphic->SetAngle( newAngle );
            }
            break;
            }

            m_view->Update( &preview );
        }

        else if( evt->IsAction( &PCB_ACTIONS::incWidth ) )
        {
            m_lineWidth += WIDTH_STEP;
            aGraphic->SetWidth( m_lineWidth );
            m_view->Update( &preview );
        }

        else if( evt->IsAction( &PCB_ACTIONS::decWidth ) && m_lineWidth > WIDTH_STEP )
        {
            m_lineWidth -= WIDTH_STEP;
            aGraphic->SetWidth( m_lineWidth );
            m_view->Update( &preview );
        }

        else if( evt->IsAction( &PCB_ACTIONS::arcPosture ) )
        {
            if( clockwise )
                aGraphic->SetAngle( aGraphic->GetAngle() - 3600.0 );
            else
                aGraphic->SetAngle( aGraphic->GetAngle() + 3600.0 );

            clockwise = !clockwise;
            m_view->Update( &preview );
        }
    }

    m_controls->ShowCursor( false );
    m_controls->SetSnapping( false );
    m_controls->SetAutoPan( false );
    m_controls->CaptureCursor( false );
    m_view->Remove( &preview );

    return ( step > SET_ORIGIN );
}


std::unique_ptr<ZONE_CONTAINER> DRAWING_TOOL::createNewZone(
        bool aKeepout )
{
    const auto& board = *getModel<BOARD>();

    // Get the current default settings for zones
    ZONE_SETTINGS zoneInfo = m_frame->GetZoneSettings();
    zoneInfo.m_CurrentZone_Layer = m_frame->GetScreen()->m_Active_Layer;
    zoneInfo.m_NetcodeSelection = board.GetHighLightNetCode();
    zoneInfo.SetIsKeepout( aKeepout );

    m_controls->SetAutoPan( true );
    m_controls->CaptureCursor( true );

    // Show options dialog
    ZONE_EDIT_T dialogResult;

    if( aKeepout )
        dialogResult = InvokeKeepoutAreaEditor( m_frame, &zoneInfo );
    else
    {
        if( IsCopperLayer( zoneInfo.m_CurrentZone_Layer ) )
            dialogResult = InvokeCopperZonesEditor( m_frame, &zoneInfo );
        else
            dialogResult = InvokeNonCopperZonesEditor( m_frame, NULL, &zoneInfo );
    }

    if( dialogResult == ZONE_ABORT )
    {
        m_controls->SetAutoPan( false );
        m_controls->CaptureCursor( false );
        return nullptr;
    }

    auto newZone = std::make_unique<ZONE_CONTAINER>( m_board );

    // Apply the selected settings
    zoneInfo.ExportSetting( *newZone );

    return newZone;
}


std::unique_ptr<ZONE_CONTAINER> DRAWING_TOOL::createZoneFromExisting(
        const ZONE_CONTAINER& aSrcZone )
{
    auto newZone = std::make_unique<ZONE_CONTAINER>( m_board );

    ZONE_SETTINGS zoneSettings;
    zoneSettings << aSrcZone;

    zoneSettings.ExportSetting( *newZone );

    return newZone;
}


bool DRAWING_TOOL::getSourceZoneForAction( ZONE_MODE aMode, ZONE_CONTAINER*& aZone )
{
    aZone = nullptr;

    // not an action that needs a source zone
    if( aMode == ZONE_MODE::ADD )
        return true;

    SELECTION_TOOL* selTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    const SELECTION& selection = selTool->GetSelection();

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true );

    // we want a single zone
    if( selection.Size() != 1 )
        return false;

    aZone = dyn_cast<ZONE_CONTAINER*>( selection[0] );

    // expected a zone, but didn't get one
    if( !aZone )
        return false;

    return true;
}


void DRAWING_TOOL::performZoneCutout( ZONE_CONTAINER& aExistingZone, ZONE_CONTAINER& aCutout )
{
    // Copy cutout corners into existing zone
    for( int ii = 0; ii < aCutout.GetNumCorners(); ii++ )
    {
        aExistingZone.AppendCorner( aCutout.GetCornerPosition( ii ) );
    }

    // Close the current corner list
    aExistingZone.Outline()->CloseLastContour();

    m_board->OnAreaPolygonModified( nullptr, &aExistingZone );

    // Re-fill if needed
    if( aExistingZone.IsFilled() )
    {
        SELECTION_TOOL* selTool = m_toolMgr->GetTool<SELECTION_TOOL>();

        auto& selection = selTool->GetSelection();

        selection.Clear();
        selection.Add( &aExistingZone );

        m_toolMgr->RunAction( PCB_ACTIONS::zoneFill, true );
    }
}


int DRAWING_TOOL::drawZone( bool aKeepout, ZONE_MODE aMode )
{
    std::unique_ptr<ZONE_CONTAINER> zone;
    DRAWSEGMENT line45;
    DRAWSEGMENT* helperLine = NULL;  // we will need more than one helper line
    BOARD_COMMIT commit( m_frame );
    ZONE_CONTAINER* sourceZone = nullptr;

    // get a source zone, if we need one
    if( !getSourceZoneForAction( aMode, sourceZone ) )
        return 0;

    // Add a VIEW_GROUP that serves as a preview for the new item
    SELECTION preview;
    m_view->Add( &preview );

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_controls->ShowCursor( true );
    m_controls->SetSnapping( true );

    Activate();

    VECTOR2I origin;
    int numPoints = 0;
    bool direction45 = false;       // 45 degrees only mode

    // Main loop: keep receiving events
    while( OPT_TOOL_EVENT evt = Wait() )
    {
        VECTOR2I cursorPos = m_controls->GetCursorPosition();

        // Enable 45 degrees lines only mode by holding control
        if( direction45 != ( evt->Modifier( MD_CTRL ) && numPoints > 0 ) )
        {
            direction45 = evt->Modifier( MD_CTRL );

            if( direction45 )
            {
                preview.Add( &line45 );
                make45DegLine( helperLine, &line45 );
            }
            else
            {
                preview.Remove( &line45 );
                helperLine->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
            }

            m_view->Update( &preview );
        }

        if( TOOL_EVT_UTILS::IsCancelInteractive( *evt ) )
        {
            if( numPoints > 0 )         // cancel the current zone
            {
                zone = nullptr;
                m_controls->SetAutoPan( false );
                m_controls->CaptureCursor( false );

                if( direction45 )
                {
                    preview.Remove( &line45 );
                    direction45 = false;
                }

                preview.FreeItems();
                m_view->Update( &preview );

                numPoints = 0;
            }
            else                        // there is no zone currently drawn - just stop the tool
                break;

            if( evt->IsActivate() )  // now finish unconditionally
                break;
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu();
        }
        else if( evt->IsClick( BUT_LEFT ) || evt->IsDblClick( BUT_LEFT ) )
        {
            // Check if it is double click / closing line (so we have to finish the zone)
            if( evt->IsDblClick( BUT_LEFT ) || ( numPoints > 0 && cursorPos == origin ) )
            {
                if( numPoints > 2 )     // valid zone consists of more than 2 points
                {
                    assert( zone->GetNumCorners() > 2 );

                    // Finish the zone
                    if( direction45 )
                        zone->AppendCorner( cursorPos == origin ? line45.GetStart() : line45.GetEnd() );

                    zone->Outline()->CloseLastContour();
                    zone->Outline()->RemoveNullSegments();
                    zone->Outline()->Hatch();

                    if( !aKeepout )
                        static_cast<PCB_EDIT_FRAME*>( m_frame )->Fill_Zone( zone.get() );

                    if( aMode == ZONE_MODE::CUTOUT )
                    {
                        // For cutouts, subtract from the source
                        commit.Modify( sourceZone );

                        performZoneCutout( *sourceZone, *zone );

                        commit.Push( _( "Add a zone cutout" ) );
                    }
                    else
                    {
                        // Add the zone as a new board item
                        commit.Add( zone.release() );
                        commit.Push( _( "Draw a zone" ) );
                    }
                }

                // if kept, this was released. if still not null,
                // this zone is now unwanted and can be removed
                zone = nullptr;

                numPoints = 0;
                m_controls->SetAutoPan( false );
                m_controls->CaptureCursor( false );

                if( direction45 )
                {
                    preview.Remove( &line45 );
                    direction45 = false;
                }

                preview.FreeItems();
                m_view->Update( &preview );
            }
            else
            {
                if( numPoints == 0 )        // it's the first click
                {
                    if( sourceZone )
                    {
                        zone = createZoneFromExisting( *sourceZone );
                    }
                    else
                    {
                        zone = createNewZone( aKeepout );
                    }

                    if( !zone )
                    {
                        continue;
                    }

                    m_frame->GetGalCanvas()->SetTopLayer( zone->GetLayer() );

                    // Add the first point
                    zone->Outline()->Start( zone->GetLayer(),
                                            cursorPos.x, cursorPos.y,
                                            zone->GetHatchStyle() );
                    origin = cursorPos;

                    // Helper line represents the currently drawn line of the zone polygon
                    helperLine = new DRAWSEGMENT;
                    helperLine->SetShape( S_SEGMENT );
                    helperLine->SetWidth( 1 );
                    helperLine->SetLayer( zone->GetLayer() );
                    helperLine->SetStart( wxPoint( cursorPos.x, cursorPos.y ) );
                    helperLine->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
                    line45 = *helperLine;

                    preview.Add( helperLine );
                }
                else
                {
                    zone->AppendCorner( helperLine->GetEnd() );
                    helperLine = new DRAWSEGMENT( *helperLine );
                    helperLine->SetStart( helperLine->GetEnd() );
                    preview.Add( helperLine );
                }

                ++numPoints;
                m_view->Update( &preview );
            }
        }

        else if( evt->IsMotion() && numPoints > 0 )
        {
            // 45 degree lines
            if( direction45 )
                make45DegLine( helperLine, &line45 );
            else
                helperLine->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );

            m_view->Update( &preview );
        }
    }

    m_controls->ShowCursor( false );
    m_controls->SetSnapping( false );
    m_controls->SetAutoPan( false );
    m_controls->CaptureCursor( false );
    m_view->Remove( &preview );

    m_frame->SetToolID( ID_NO_TOOL_SELECTED, wxCURSOR_DEFAULT, wxEmptyString );

    return 0;
}


void DRAWING_TOOL::make45DegLine( DRAWSEGMENT* aSegment, DRAWSEGMENT* aHelper ) const
{
    VECTOR2I cursorPos = m_controls->GetCursorPosition();
    VECTOR2I origin( aSegment->GetStart() );
    DIRECTION_45 direction( origin - cursorPos );
    SHAPE_LINE_CHAIN newChain = direction.BuildInitialTrace( origin, cursorPos );

    if( newChain.PointCount() > 2 )
    {
        aSegment->SetEnd( wxPoint( newChain.Point( -2 ).x, newChain.Point( -2 ).y ) );
        aHelper->SetStart( wxPoint( newChain.Point( -2 ).x, newChain.Point( -2 ).y ) );
        aHelper->SetEnd( wxPoint( newChain.Point( -1 ).x, newChain.Point( -1 ).y ) );
    }
    else
    {
        aSegment->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
        aHelper->SetStart( wxPoint( cursorPos.x, cursorPos.y ) );
        aHelper->SetEnd( wxPoint( cursorPos.x, cursorPos.y ) );
    }
}


void DRAWING_TOOL::SetTransitions()
{
    Go( &DRAWING_TOOL::DrawLine,         PCB_ACTIONS::drawLine.MakeEvent() );
    Go( &DRAWING_TOOL::DrawCircle,       PCB_ACTIONS::drawCircle.MakeEvent() );
    Go( &DRAWING_TOOL::DrawArc,          PCB_ACTIONS::drawArc.MakeEvent() );
    Go( &DRAWING_TOOL::DrawDimension,    PCB_ACTIONS::drawDimension.MakeEvent() );
    Go( &DRAWING_TOOL::DrawZone,         PCB_ACTIONS::drawZone.MakeEvent() );
    Go( &DRAWING_TOOL::DrawKeepout,      PCB_ACTIONS::drawKeepout.MakeEvent() );
    Go( &DRAWING_TOOL::DrawZoneCutout,   PCB_ACTIONS::drawZoneCutout.MakeEvent() );
    Go( &DRAWING_TOOL::DrawSimilarZone,  PCB_ACTIONS::drawSimilarZone.MakeEvent() );
    Go( &DRAWING_TOOL::PlaceText,        PCB_ACTIONS::placeText.MakeEvent() );
    Go( &DRAWING_TOOL::PlaceDXF,         PCB_ACTIONS::placeDXF.MakeEvent() );
    Go( &DRAWING_TOOL::SetAnchor,        PCB_ACTIONS::setAnchor.MakeEvent() );
}


int DRAWING_TOOL::getSegmentWidth( unsigned int aLayer ) const
{
    assert( m_board );

    if( aLayer == Edge_Cuts )
        return m_board->GetDesignSettings().m_EdgeSegmentWidth;
    else if( m_editModules )
        return m_board->GetDesignSettings().m_ModuleSegmentWidth;
    else
        return m_board->GetDesignSettings().m_DrawSegmentWidth;
}


LAYER_ID DRAWING_TOOL::getDrawingLayer() const
{
    LAYER_ID layer = m_frame->GetActiveLayer();

    if( IsCopperLayer( layer ) )
    {
        if( layer == F_Cu )
            layer = F_SilkS;
        else if( layer == B_Cu )
            layer = B_SilkS;
        else
            layer = Dwgs_User;

        m_frame->SetActiveLayer( layer );
    }

    return layer;
}

const unsigned int DRAWING_TOOL::WIDTH_STEP = 100000;

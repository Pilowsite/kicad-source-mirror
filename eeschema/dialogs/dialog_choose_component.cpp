/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014 Henner Zeller <h.zeller@acm.org>
 * Copyright (C) 2016-2019 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <algorithm>
#include <class_libentry.h>
#include <class_library.h>
#include <dialog_choose_component.h>
#include <eeschema_settings.h>
#include <kiface_i.h>
#include <sch_base_frame.h>
#include <symbol_lib_table.h>
#include <template_fieldnames.h>
#include <widgets/footprint_preview_widget.h>
#include <widgets/footprint_select_widget.h>
#include <widgets/lib_tree.h>
#include <widgets/symbol_preview_widget.h>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/timer.h>
#include <wx/utils.h>

std::mutex DIALOG_CHOOSE_COMPONENT::g_Mutex;


DIALOG_CHOOSE_COMPONENT::DIALOG_CHOOSE_COMPONENT( SCH_BASE_FRAME* aParent, const wxString& aTitle,
                                                  SYMBOL_TREE_MODEL_ADAPTER::PTR& aAdapter,
                                                  int aDeMorganConvert, bool aAllowFieldEdits,
                                                  bool aShowFootprints, bool aAllowBrowser )
        : DIALOG_SHIM( aParent, wxID_ANY, aTitle, wxDefaultPosition, wxDefaultSize,
                       wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
          m_symbol_preview( nullptr ),
          m_browser_button( nullptr ),
          m_hsplitter( nullptr ),
          m_vsplitter( nullptr ),
          m_fp_sel_ctrl( nullptr ),
          m_fp_preview( nullptr ),
          m_keepSymbol( nullptr ),
          m_useUnits( nullptr ),
          m_tree( nullptr ),
          m_details( nullptr ),
          m_parent( aParent ),
          m_deMorganConvert( aDeMorganConvert >= 0 ? aDeMorganConvert : 0 ),
          m_allow_field_edits( aAllowFieldEdits ),
          m_show_footprints( aShowFootprints ),
          m_external_browser_requested( false )
{
    auto sizer = new wxBoxSizer( wxVERTICAL );

    // Use a slightly different layout, with a details pane spanning the entire window,
    // if we're not showing footprints.
    if( aShowFootprints )
    {
        m_hsplitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE );

        //Avoid the splitter window being assigned as the Parent to additional windows
        m_hsplitter->SetExtraStyle( wxWS_EX_TRANSIENT );

        sizer->Add( m_hsplitter, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5 );
    }
    else
    {
        m_vsplitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE );

        m_hsplitter = new wxSplitterWindow( m_vsplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE );

        //Avoid the splitter window being assigned as the Parent to additional windows
        m_hsplitter->SetExtraStyle( wxWS_EX_TRANSIENT );

        auto detailsPanel = new wxPanel( m_vsplitter );
        auto detailsSizer = new wxBoxSizer( wxVERTICAL );
        detailsPanel->SetSizer( detailsSizer );

        m_details = new wxHtmlWindow( detailsPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxHW_SCROLLBAR_AUTO );
        detailsSizer->Add( m_details, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5 );
        detailsPanel->Layout();
        detailsSizer->Fit( detailsPanel );

        m_vsplitter->SetSashGravity( 0.5 );
        m_vsplitter->SetMinimumPaneSize( 20 );
        m_vsplitter->SplitHorizontally( m_hsplitter, detailsPanel );

        sizer->Add( m_vsplitter, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5 );
    }

    m_tree = new LIB_TREE( m_hsplitter, Prj().SchSymbolLibTable(), aAdapter,
                           LIB_TREE::WIDGETS::ALL, m_details );

    m_hsplitter->SetSashGravity( 0.8 );
    m_hsplitter->SetMinimumPaneSize( 20 );
    m_hsplitter->SplitVertically( m_tree,  ConstructRightPanel( m_hsplitter ) );

    m_dbl_click_timer = new wxTimer( this );

    auto buttonsSizer = new wxBoxSizer( wxHORIZONTAL );

    if( aAllowBrowser )
    {
        m_browser_button = new wxButton( this, wxID_ANY, _( "Select with Browser" ) );
        buttonsSizer->Add( m_browser_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5 );
    }

    auto sdbSizer = new wxStdDialogButtonSizer();
    auto okButton = new wxButton( this, wxID_OK );
    auto cancelButton = new wxButton( this, wxID_CANCEL );
    sdbSizer->AddButton( okButton );
    sdbSizer->AddButton( cancelButton );
    sdbSizer->Realize();

    buttonsSizer->Add( sdbSizer, 1, wxALL, 5 );

    sizer->Add( buttonsSizer, 0, wxEXPAND | wxLEFT, 5 );
    SetSizer( sizer );

    Layout();

    EESCHEMA_SETTINGS* cfg = dynamic_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() );
    wxASSERT( cfg );

    if( cfg )
    {
        EESCHEMA_SETTINGS::PANEL_SYM_CHOOSER& panelCfg = cfg->m_SymChooserPanel;

        // We specify the width of the right window (m_symbol_view_panel), because specify
        // the width of the left window does not work as expected when SetSashGravity() is called
        m_hsplitter->SetSashPosition( panelCfg.sash_pos_h > 0 ? panelCfg.sash_pos_h :
                                                                HorizPixelsFromDU( 220 ) );

        if( m_vsplitter )
            m_vsplitter->SetSashPosition( panelCfg.sash_pos_v > 0 ? panelCfg.sash_pos_v :
                                                                    VertPixelsFromDU( 230 ) );

        wxSize dlgSize( panelCfg.width > 0 ? panelCfg.width : HorizPixelsFromDU( 390 ),
                        panelCfg.height > 0 ? panelCfg.height : VertPixelsFromDU( 300 ) );
        SetSize( dlgSize );
    }

    SetInitialFocus( m_tree );
    okButton->SetDefault();

    Bind( wxEVT_INIT_DIALOG, &DIALOG_CHOOSE_COMPONENT::OnInitDialog, this );
    Bind( wxEVT_TIMER, &DIALOG_CHOOSE_COMPONENT::OnCloseTimer, this, m_dbl_click_timer->GetId() );
    Bind( COMPONENT_PRESELECTED, &DIALOG_CHOOSE_COMPONENT::OnComponentPreselected, this );
    Bind( COMPONENT_SELECTED, &DIALOG_CHOOSE_COMPONENT::OnComponentSelected, this );

    if( m_browser_button )
        m_browser_button->Bind( wxEVT_COMMAND_BUTTON_CLICKED,
                                &DIALOG_CHOOSE_COMPONENT::OnUseBrowser, this );

    if( m_fp_sel_ctrl )
        m_fp_sel_ctrl->Bind( EVT_FOOTPRINT_SELECTED,
                             &DIALOG_CHOOSE_COMPONENT::OnFootprintSelected, this );

    if( m_details )
        m_details->Connect( wxEVT_CHAR_HOOK,
                            wxKeyEventHandler( DIALOG_CHOOSE_COMPONENT::OnCharHook ),
                            NULL, this );
}


DIALOG_CHOOSE_COMPONENT::~DIALOG_CHOOSE_COMPONENT()
{
    Unbind( wxEVT_INIT_DIALOG, &DIALOG_CHOOSE_COMPONENT::OnInitDialog, this );
    Unbind( wxEVT_TIMER, &DIALOG_CHOOSE_COMPONENT::OnCloseTimer, this );
    Unbind( COMPONENT_PRESELECTED, &DIALOG_CHOOSE_COMPONENT::OnComponentPreselected, this );
    Unbind( COMPONENT_SELECTED, &DIALOG_CHOOSE_COMPONENT::OnComponentSelected, this );

    if( m_browser_button )
        m_browser_button->Unbind( wxEVT_COMMAND_BUTTON_CLICKED,
                                  &DIALOG_CHOOSE_COMPONENT::OnUseBrowser, this );

    if( m_fp_sel_ctrl )
        m_fp_sel_ctrl->Unbind( EVT_FOOTPRINT_SELECTED,
                               &DIALOG_CHOOSE_COMPONENT::OnFootprintSelected, this );

    if( m_details )
        m_details->Disconnect( wxEVT_CHAR_HOOK,
                               wxKeyEventHandler( DIALOG_CHOOSE_COMPONENT::OnCharHook ),
                               NULL, this );

    // I am not sure the following two lines are necessary, but they will not hurt anyone
    m_dbl_click_timer->Stop();
    delete m_dbl_click_timer;

    auto cfg = dynamic_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() );

    cfg->m_SymChooserPanel.width = GetSize().x;
    cfg->m_SymChooserPanel.height = GetSize().y;

    cfg->m_SymChooserPanel.keep_symbol = m_keepSymbol->GetValue();
    cfg->m_SymChooserPanel.place_all_units = m_useUnits->GetValue();

    cfg->m_SymChooserPanel.sash_pos_h = m_hsplitter->GetSashPosition();

    if( m_vsplitter )
        cfg->m_SymChooserPanel.sash_pos_v = m_vsplitter->GetSashPosition();
}


wxPanel* DIALOG_CHOOSE_COMPONENT::ConstructRightPanel( wxWindow* aParent )
{
    wxPanel*                     panel = new wxPanel( aParent );
    wxBoxSizer*                  sizer = new wxBoxSizer( wxVERTICAL );
    EDA_DRAW_PANEL_GAL::GAL_TYPE backend = m_parent->GetCanvas()->GetBackend();

    m_symbol_preview = new SYMBOL_PREVIEW_WIDGET( panel, Kiway(), backend );
    m_symbol_preview->SetLayoutDirection( wxLayout_LeftToRight );

    if( m_show_footprints )
    {
        FOOTPRINT_LIST* fp_list = FOOTPRINT_LIST::GetInstance( Kiway() );

        sizer->Add( m_symbol_preview, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 5 );

        if ( fp_list )
        {
            if( m_allow_field_edits )
                m_fp_sel_ctrl = new FOOTPRINT_SELECT_WIDGET( panel, fp_list, true );

            m_fp_preview = new FOOTPRINT_PREVIEW_WIDGET( panel, Kiway() );
        }

        if( m_fp_sel_ctrl )
            sizer->Add( m_fp_sel_ctrl, 0, wxEXPAND | wxBOTTOM | wxTOP | wxRIGHT, 5 );

        if( m_fp_preview )
            sizer->Add( m_fp_preview, 1, wxEXPAND | wxBOTTOM | wxRIGHT, 5 );
    }
    else
    {
        sizer->Add( m_symbol_preview, 1, wxEXPAND | wxTOP | wxRIGHT, 5 );
    }

    auto cfg = dynamic_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() );

    m_keepSymbol = new wxCheckBox( panel, 1000, _("Multi-Symbol Placement"), wxDefaultPosition,
                                   wxDefaultSize, wxALIGN_RIGHT );
    m_keepSymbol->SetValue( cfg->m_SymChooserPanel.keep_symbol );
    m_keepSymbol->SetToolTip( _( "Place multiple copies of the symbol." ) );

    m_useUnits = new wxCheckBox( panel, 1000, _("Place all units"), wxDefaultPosition,
                                 wxDefaultSize, wxALIGN_RIGHT );
    m_useUnits->SetValue( cfg->m_SymChooserPanel.place_all_units );
    m_useUnits->SetToolTip( _( "Sequentially place all units of the symbol." ) );

    auto fgSizer = new wxFlexGridSizer( 0, 2, 0, 1 );
    fgSizer->AddGrowableCol( 0 );
    fgSizer->SetFlexibleDirection( wxBOTH );
    fgSizer->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

    fgSizer->Add( 0, 0, 1, wxEXPAND );
    fgSizer->Add( m_keepSymbol, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxBOTTOM|wxRIGHT|wxLEFT, 5 );
    fgSizer->Add( 0, 0, 1, wxEXPAND );
    fgSizer->Add( m_useUnits, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxBOTTOM|wxRIGHT|wxLEFT, 5 );

    sizer->Add( fgSizer, 0, wxALL | wxEXPAND, 5 );
    panel->SetSizer( sizer );
    panel->Layout();
    sizer->Fit( panel );

    return panel;
}


void DIALOG_CHOOSE_COMPONENT::OnInitDialog( wxInitDialogEvent& aEvent )
{
    if( m_fp_preview && m_fp_preview->IsInitialized() )
    {
        // This hides the GAL panel and shows the status label
        m_fp_preview->SetStatusText( wxEmptyString );
    }

    if( m_fp_sel_ctrl )
        m_fp_sel_ctrl->Load( Kiway(), Prj() );
}


void DIALOG_CHOOSE_COMPONENT::OnCharHook( wxKeyEvent& e )
{
    if( m_details && e.GetKeyCode() == 'C' && e.ControlDown() &&
        !e.AltDown() && !e.ShiftDown() && !e.MetaDown() )
    {
        wxString txt = m_details->SelectionToText();

        if( wxTheClipboard->Open() )
        {
            wxTheClipboard->SetData( new wxTextDataObject( txt ) );
            wxTheClipboard->Close();
        }
    }
    else
    {
        e.Skip();
    }
}


LIB_ID DIALOG_CHOOSE_COMPONENT::GetSelectedLibId( int* aUnit ) const
{
    return m_tree->GetSelectedLibId( aUnit );
}


void DIALOG_CHOOSE_COMPONENT::OnUseBrowser( wxCommandEvent& aEvent )
{
    m_external_browser_requested = true;
    EndQuasiModal( wxID_OK );
}


void DIALOG_CHOOSE_COMPONENT::OnCloseTimer( wxTimerEvent& aEvent )
{
    // Hack handler because of eaten MouseUp event. See
    // DIALOG_CHOOSE_COMPONENT::OnComponentSelected for the beginning
    // of this spaghetti noodle.

    auto state = wxGetMouseState();

    if( state.LeftIsDown() )
    {
        // Mouse hasn't been raised yet, so fire the timer again. Otherwise the
        // purpose of this timer is defeated.
        m_dbl_click_timer->StartOnce( DIALOG_CHOOSE_COMPONENT::DblClickDelay );
    }
    else
    {
        EndQuasiModal( wxID_OK );
    }
}


void DIALOG_CHOOSE_COMPONENT::ShowFootprintFor( LIB_ID const& aLibId )
{
    if( !m_fp_preview || !m_fp_preview->IsInitialized() )
        return;

    LIB_PART* symbol = nullptr;

    try
    {
        symbol = Prj().SchSymbolLibTable()->LoadSymbol( aLibId );
    }
    catch( const IO_ERROR& ioe )
    {
        wxLogError( wxString::Format( _( "Error loading symbol %s from library %s.\n\n%s" ),
                                      aLibId.GetLibItemName().wx_str(),
                                      aLibId.GetLibNickname().wx_str(),
                                      ioe.What() ) );
    }

    if( !symbol )
        return;

    LIB_FIELD* fp_field = symbol->GetField( FOOTPRINT );
    wxString   fp_name = fp_field ? fp_field->GetFullText() : wxString( "" );

    ShowFootprint( fp_name );
}


void DIALOG_CHOOSE_COMPONENT::ShowFootprint( wxString const& aName )
{
    if( !m_fp_preview || !m_fp_preview->IsInitialized() )
        return;

    if( aName == wxEmptyString )
    {
        m_fp_preview->SetStatusText( _( "No footprint specified" ) );
    }
    else
    {
        LIB_ID lib_id;

        if( lib_id.Parse( aName, LIB_ID::ID_PCB ) == -1 && lib_id.IsValid() )
        {
            m_fp_preview->ClearStatus();
            m_fp_preview->CacheFootprint( lib_id );
            m_fp_preview->DisplayFootprint( lib_id );
        }
        else
        {
            m_fp_preview->SetStatusText( _( "Invalid footprint specified" ) );
        }
    }
}


void DIALOG_CHOOSE_COMPONENT::PopulateFootprintSelector( LIB_ID const& aLibId )
{
    if( !m_fp_sel_ctrl )
        return;

    m_fp_sel_ctrl->ClearFilters();

    LIB_PART* symbol = nullptr;

    if( aLibId.IsValid() )
    {
        try
        {
            symbol = Prj().SchSymbolLibTable()->LoadSymbol( aLibId );
        }
        catch( const IO_ERROR& ioe )
        {
            wxLogError( wxString::Format( _( "Error occurred loading symbol %s from library %s."
                                             "\n\n%s" ),
                                          aLibId.GetLibItemName().wx_str(),
                                          aLibId.GetLibNickname().wx_str(),
                                          ioe.What() ) );
        }
    }

    if( symbol != nullptr )
    {
        LIB_PINS   temp_pins;
        LIB_FIELD* fp_field = symbol->GetField( FOOTPRINT );
        wxString   fp_name = fp_field ? fp_field->GetFullText() : wxString( "" );

        symbol->GetPins( temp_pins );

        m_fp_sel_ctrl->FilterByPinCount( temp_pins.size() );
        m_fp_sel_ctrl->FilterByFootprintFilters( symbol->GetFootprints(), true );
        m_fp_sel_ctrl->SetDefaultFootprint( fp_name );
        m_fp_sel_ctrl->UpdateList();
        m_fp_sel_ctrl->Enable();
    }
    else
    {
        m_fp_sel_ctrl->UpdateList();
        m_fp_sel_ctrl->Disable();
    }
}


void DIALOG_CHOOSE_COMPONENT::OnFootprintSelected( wxCommandEvent& aEvent )
{
    m_fp_override = aEvent.GetString();

    m_field_edits.erase( std::remove_if( m_field_edits.begin(), m_field_edits.end(),
                                         []( std::pair<int, wxString> const& i )
                                         {
                                             return i.first == FOOTPRINT;
                                         } ),
                         m_field_edits.end() );

    m_field_edits.emplace_back( std::make_pair( FOOTPRINT, m_fp_override ) );

    ShowFootprint( m_fp_override );
}


void DIALOG_CHOOSE_COMPONENT::OnComponentPreselected( wxCommandEvent& aEvent )
{
    int unit = 0;

    LIB_ID id = m_tree->GetSelectedLibId( &unit );

    if( id.IsValid() )
    {
        m_symbol_preview->DisplaySymbol( id, unit );

        ShowFootprintFor( id );
        PopulateFootprintSelector( id );
    }
    else
    {
        m_symbol_preview->SetStatusText( _( "No symbol selected" ) );

        if( m_fp_preview && m_fp_preview->IsInitialized() )
            m_fp_preview->SetStatusText( wxEmptyString );

        PopulateFootprintSelector( id );
    }
}


void DIALOG_CHOOSE_COMPONENT::OnComponentSelected( wxCommandEvent& aEvent )
{
    if( m_tree->GetSelectedLibId().IsValid() )
    {
        // Got a selection. We can't just end the modal dialog here, because
        // wx leaks some events back to the parent window (in particular, the
        // MouseUp following a double click).
        //
        // NOW, here's where it gets really fun. wxTreeListCtrl eats MouseUp.
        // This isn't really feasible to bypass without a fully custom
        // wxDataViewCtrl implementation, and even then might not be fully
        // possible (docs are vague). To get around this, we use a one-shot
        // timer to schedule the dialog close.
        //
        // See DIALOG_CHOOSE_COMPONENT::OnCloseTimer for the other end of this
        // spaghetti noodle.
        m_dbl_click_timer->StartOnce( DIALOG_CHOOSE_COMPONENT::DblClickDelay );
    }
}



#include "MultiIconValueIndicator.h"

#include <GG/Texture.h>

#include "../util/i18n.h"
#include "../util/Logger.h"
#include "../universe/PopCenter.h"
#include "../universe/UniverseObject.h"
#include "../universe/Enums.h"
#include "../client/human/HumanClientApp.h"
#include "ClientUI.h"
#include "CUIControls.h"

namespace {
    const int   EDGE_PAD(3);

    int         IconSpacing()
    { return ClientUI::Pts(); }
    GG::X       IconWidth()
    { return GG::X(IconSpacing()*2); }
    GG::Y       IconHeight()
    { return GG::Y(IconSpacing()*2); }
}

MultiIconValueIndicator::MultiIconValueIndicator(GG::X w) :
    MultiIconValueIndicator(w, {}, {})
{}

MultiIconValueIndicator::MultiIconValueIndicator(
    GG::X w, int object_id,
    const std::vector<std::pair<MeterType, MeterType>>& meter_types) :
    MultiIconValueIndicator(w, std::vector<int>{object_id}, meter_types)
{}

MultiIconValueIndicator::MultiIconValueIndicator(
    GG::X w, const std::vector<int>& object_ids,
    const std::vector<std::pair<MeterType, MeterType>>& meter_types) :
    GG::Wnd(GG::X0, GG::Y0, w, GG::Y1, GG::INTERACTIVE),
    m_meter_types(meter_types),
    m_object_ids(object_ids)
{}

void MultiIconValueIndicator::CompleteConstruction() {
    GG::Wnd::CompleteConstruction();

    SetName("MultiIconValueIndicator");

    GG::X x(EDGE_PAD);
    for (const auto& meter_type : m_meter_types) {
        const MeterType PRIMARY_METER_TYPE = meter_type.first;
        // get icon texture.
        auto texture = ClientUI::MeterIcon(PRIMARY_METER_TYPE);

        // special case for population meter for an indicator showing only a
        // single popcenter: icon is species icon, rather than generic pop icon
        if (PRIMARY_METER_TYPE == METER_POPULATION && m_object_ids.size() == 1) {
            if (auto pc = GetPopCenter(*m_object_ids.begin()))
                texture = ClientUI::SpeciesIcon(pc->SpeciesName());
        }

        m_icons.push_back(GG::Wnd::Create<StatisticIcon>(texture, 0.0, 3, false, IconWidth(), IconHeight()));
        GG::Pt icon_ul(x, GG::Y(EDGE_PAD));
        GG::Pt icon_lr = icon_ul + GG::Pt(IconWidth(), IconHeight() + ClientUI::Pts()*3/2);
        m_icons.back()->SizeMove(icon_ul, icon_lr);
        auto meter = meter_type.first;
        auto meter_string = boost::lexical_cast<std::string>(meter_type.first);
        m_icons.back()->RightClickedSignal.connect([this, meter, meter_string](const GG::Pt& pt) {
            auto popup = GG::Wnd::Create<CUIPopupMenu>(pt.x, pt.y);

            auto pc = GetPopCenter(*(this->m_object_ids.begin()));
            if (meter == METER_POPULATION && pc && this->m_object_ids.size() == 1) {
                auto species_name = pc->SpeciesName();
                if (!species_name.empty()) {
                    auto zoom_species_action = [species_name]() { ClientUI::GetClientUI()->ZoomToSpecies(species_name); };
                    std::string species_label = boost::io::str(FlexibleFormat(UserString("ENC_LOOKUP")) %
                                                                              UserString(species_name));
                    popup->AddMenuItem(GG::MenuItem(species_label, false, false, zoom_species_action));
                }
            }


            auto zoom_article_action = [meter_string]() { ClientUI::GetClientUI()->ZoomToMeterTypeArticle(meter_string);};
            std::string popup_label = boost::io::str(FlexibleFormat(UserString("ENC_LOOKUP")) %
                                                                    UserString(meter_string));
            popup->AddMenuItem(GG::MenuItem(popup_label, false, false, zoom_article_action));
            popup->Run();
        });
        AttachChild(m_icons.back());
        x += IconWidth() + IconSpacing();
    }
    if (!m_icons.empty())
        Resize(GG::Pt(Width(), EDGE_PAD + IconHeight() + ClientUI::Pts()*3/2));
    Update();
}

bool MultiIconValueIndicator::Empty() const
{ return m_object_ids.empty(); }

void MultiIconValueIndicator::Render() {
    GG::Pt ul = UpperLeft();
    GG::Pt lr = LowerRight();

    // outline of whole control
    GG::FlatRectangle(ul, lr, ClientUI::WndColor(), ClientUI::WndOuterBorderColor(), 1);
}

void MultiIconValueIndicator::MouseWheel(const GG::Pt& pt, int move, GG::Flags<GG::ModKey> mod_keys)
{ ForwardEventToParent(); }

void MultiIconValueIndicator::Update() {
    if (m_icons.size() != m_meter_types.size()) {
        ErrorLogger() << "MultiIconValueIndicator::Update has inconsitent numbers of icons and meter types";
        return;
    }

    for (std::size_t i = 0; i < m_icons.size(); ++i) {
        assert(m_icons[i]);
        double total = 0.0;
        for (int object_id : m_object_ids) {
            auto obj = GetUniverseObject(object_id);
            if (!obj) {
                ErrorLogger() << "MultiIconValueIndicator::Update couldn't get object with id " << object_id;
                continue;
            }
            //DebugLogger() << "MultiIconValueIndicator::Update object:";
            //DebugLogger() << obj->Dump();
            auto type = m_meter_types[i].first;
            double value = obj->InitialMeterValue(type);
            // Supply is a special case: the only thing that matters is the highest value.
            if (type == METER_SUPPLY)
                total = std::max(total, value);
            else
                total += value;
        }
        m_icons[i]->SetValue(total);
    }
}

void MultiIconValueIndicator::SetToolTip(MeterType meter_type, const std::shared_ptr<GG::BrowseInfoWnd>& browse_wnd) {
    for (unsigned int i = 0; i < m_icons.size(); ++i)
        if (m_meter_types.at(i).first == meter_type)
            m_icons.at(i)->SetBrowseInfoWnd(browse_wnd);
}

void MultiIconValueIndicator::ClearToolTip(MeterType meter_type) {
    for (unsigned int i = 0; i < m_icons.size(); ++i)
        if (m_meter_types.at(i).first == meter_type)
            m_icons.at(i)->ClearBrowseInfoWnd();
}

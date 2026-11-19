void test_clean_face_and_app_layouts()
{
    using namespace pico::display;
    using Frame=std::array<std::uint8_t,1024>;
    Frame clean{},flags{};
    pico::State state;
    CHECK(render(state,{clean.data(),clean.size(),128,64,MonoLayout::HorizontalMsb}));
    state.connected=true; state.motion_ready=true; state.motion_settled=true; state.dock=pico::Dock::Home;
    CHECK(render(state,{flags.data(),flags.size(),128,64,MonoLayout::HorizontalMsb}));
    CHECK(clean==flags); // diagnostic state no longer decorates the face
    for (auto screen : {pico::Screen::Settings,pico::Screen::SettingEdit,pico::Screen::DurationEdit,
                        pico::Screen::Music,pico::Screen::Tuner,pico::Screen::ReferenceTone,
                        pico::Screen::FocusSummary,pico::Screen::Utility,pico::Screen::Themes}) {
        state.screen=screen;
        for (unsigned index=0; index<11; ++index) {
            state.settings_index=index; state.music_index=index%8; state.utility_index=index%8;
            state.reference_index=index%6; state.setting_action=index%4; state.duration_action=index%4;
            state.setting_key=static_cast<pico::SettingKey>(index%7);
            state.mode=pico::Mode::Focus;
            Frame h{},v{};
            MonoSurface horizontal{h.data(),h.size(),128,64,MonoLayout::HorizontalMsb};
            MonoSurface vertical{v.data(),v.size(),128,64,MonoLayout::VerticalLsb};
            CHECK(render(state,horizontal) && render(state,vertical));
            for (unsigned y=0;y<64;++y) for (unsigned x=0;x<128;++x)
                CHECK(pixel_at(horizontal,x,y)==pixel_at(vertical,x,y));
            std::array<std::uint8_t,302> guarded{}; guarded.fill(0xa5);
            CHECK(render(state,{guarded.data()+1,300,65,33,MonoLayout::HorizontalMsb}));
            CHECK(guarded.front()==0xa5 && guarded.back()==0xa5);
        }
    }
}

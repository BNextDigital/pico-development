// Included inside the host-test namespace.
void test_icon_menu_frames()
{
    using namespace pico::display;
    using Frame = std::array<std::uint8_t, kDefaultBufferSize>;
    std::array<Frame, 9> frames{};
    for (unsigned item = 0; item < frames.size(); ++item) {
        pico::State state{};
        state.screen = pico::Screen::Menu;
        state.menu_index = item;
        MonoSurface horizontal{frames[item].data(), frames[item].size(), 128, 64,
                               MonoLayout::HorizontalMsb};
        CHECK(render(state, horizontal));
        unsigned icon_pixels = 0, caption_pixels = 0;
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 128; ++x) {
            if (x >= 48 && x < 80 && y >= 5 && y < 37)
                icon_pixels += pixel_at(horizontal, x, y);
            if (y >= 44 && y < 54) caption_pixels += pixel_at(horizontal, x, y);
        }
        CHECK(icon_pixels > 50 && caption_pixels > 20);
        for (const auto layout : {MonoLayout::VerticalLsb, MonoLayout::VerticalMsb}) {
            Frame tiled{};
            MonoSurface vertical{tiled.data(), tiled.size(), 128, 64, layout};
            CHECK(render(state, vertical));
            for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 128; ++x)
                CHECK(pixel_at(horizontal, x, y) == pixel_at(vertical, x, y));
        }
        // Exact buffer sizes and guards exercise compact and odd-size page strides.
        for (const auto layout : {MonoLayout::HorizontalMsb, MonoLayout::VerticalLsb,
                                  MonoLayout::VerticalMsb}) {
            for (const auto width : {64, 65}) {
                std::array<std::uint8_t, 512> guarded{};
                guarded.fill(0xa5);
                MonoSurface small{guarded.data() + 1, 0,
                                  static_cast<std::uint16_t>(width), 33, layout};
                small.size = required_buffer_size(small);
                CHECK(render(state, small));
                CHECK(guarded.front() == 0xa5 && guarded[small.size + 1] == 0xa5);
            }
        }
    }
    for (unsigned a = 0; a < frames.size(); ++a)
        for (unsigned b = a + 1; b < frames.size(); ++b) CHECK(frames[a] != frames[b]);
}

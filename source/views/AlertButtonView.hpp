// Stellar Updater
// Copyright (C) 2020 Nichole Mattera
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#pragma once

#include <string>

#include "ControlView.hpp"
#include "TextView.hpp"

namespace dsu::views {
    class AlertButtonView : public ControlView {
        public:
            bool isLast;

            AlertButtonView(std::string title, bool hasFocus, SDL_Rect rect);
            ~AlertButtonView();

            void setHasFocus(bool focus);

            void render(SDL_Rect rect, double dTime);
            
        private:
            TextView * _textView;
    };
}

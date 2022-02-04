//
// Created by syys on 2022/2/4.
//

#include "attax_board.h"

using namespace std;

//LOCATION--------------------------------------------------------------------------------
Loc Location::getLoc(Coord x, Coord y, Coord x_size)
{
    return x + (y << 3u);
}
int Location::getX(Loc loc, Coord x_size)
{
    return loc & (Coord)7;
}
int Location::getY(Loc loc, Coord x_size)
{
    return loc >> (Coord)7;
}

string Location::toStringMach(Loc loc, int x_size)
{
    if(loc == Board::PASS_LOC)
        return string("pass");
    if(loc == Board::NULL_LOC)
        return string("null");
    char buf[128];
    sprintf(buf,"(%d,%d)",getX(loc,x_size),getY(loc,x_size));
    return string(buf);
}

string Location::toString(Loc loc, int x_size, int y_size)
{
    if(x_size > 25*25)
        return toStringMach(loc,x_size);
    if(loc == Board::PASS_LOC)
        return string("pass");
    if(loc == Board::NULL_LOC)
        return string("null");
    const char* xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
    int x = getX(loc,x_size);
    int y = getY(loc,x_size);
    if(x >= x_size || x < 0 || y < 0 || y >= y_size)
        return toStringMach(loc,x_size);

    char buf[128];
    if(x <= 24)
        sprintf(buf,"%c%d",xChar[x],y_size-y);
    else
        sprintf(buf,"%c%c%d",xChar[x/25-1],xChar[x%25],y_size-y);
    return string(buf);
}

string Location::toString(Loc loc, const Board& b) {
    return toString(loc,b.x_size,b.y_size);
}

string Location::toStringMach(Loc loc, const Board& b) {
    return toStringMach(loc,b.x_size);
}
#include <config.h>

#include "MiniMap.hpp"
#include "GameView.hpp"
#include "MainLincity.hpp"

#include "gui/Painter.hpp"
#include "gui/Button.hpp"
#include "gui/Rect2D.hpp"
#include "gui/ComponentFactory.hpp"
#include "gui/XmlReader.hpp"
#include "gui/Event.hpp"
#include "gui_interface/mps.h"

#include "lincity/lin-city.h"
#include "lincity/engglobs.h"
#include "lincity/lctypes.h"

#include "gui/callback/Callback.hpp"

#include "gui_interface/shared_globals.h"
#include "gui_interface/screen_interface.h"

#include <set>
#include <iostream>

#include "Debug.hpp"
#include "Util.hpp"
#include "Mps.hpp"
#include "CheckButton.hpp"
#include "Dialog.hpp"

#define LC_MOUSE_LEFTBUTTON 1
#define LC_MOUSE_RIGHTBUTTON 2
#define LC_MOUSE_MIDDLEBUTTON 3
  
const char* mapViewButtons[] = { "MapViewNormal", "MapViewUB40",
                                 "MapViewPollution", "MapViewFood", "MapViewPower", "MapViewFire",
                                 "MapViewSport", "MapViewHealth", "MapViewCoal", 0
};
const char* speedButtons[] = { "SpeedPauseButton","SpeedSlowButton", "SpeedNormalButton", "SpeedFastButton", 0 };

Uint8 brightness(const Color &c)
{
  Uint16 x=c.r;
  x+=c.g;
  x+=c.b;
  return x/3;
}

Color makeGrey(const Color &c)
{
  Uint8 b=brightness(c);
  return Color(b,b,b);
}

Color light(const Color &c,Uint8 b)
{
  return Color(((Uint16(c.r)*b)>>8),
	       ((Uint16(c.g)*b)>>8),
	       ((Uint16(c.b)*b)>>8));
}

MiniMap* miniMapPtr = 0;

MiniMap* getMiniMap()
{
    return miniMapPtr;
}
    
MiniMap::MiniMap()
    : mMode(NORMAL), tilesize(2), border(0), mTexture(0)
{   
    assert( miniMapPtr == 0 );
    miniMapPtr = this;
    gameViewPoints[ 0 ].x = 0;
    gameViewPoints[ 0 ].y = 0;
    gameViewPoints[ 1 ].x = 0;
    gameViewPoints[ 1 ].y = 0;
    gameViewPoints[ 2 ].x = 0;
    gameViewPoints[ 2 ].y = 0;
    gameViewPoints[ 3 ].x = 0;
    gameViewPoints[ 3 ].y = 0;
}

MiniMap::~MiniMap() {
    if(miniMapPtr == this)
        miniMapPtr = 0;
}

void
MiniMap::parse(XmlReader& reader)
{
    // parse attributes...
    XmlReader::AttributeIterator iter(reader);
    while(iter.next()) {
        const char* name = (const char*) iter.getName();
        const char* value = (const char*) iter.getValue();

        if(strcmp(name, "width") == 0) {
            if(sscanf(value, "%f", &width) != 1) {
                std::stringstream msg;
                msg << "Couldn't parse width attribute (" << value << ").";
                throw std::runtime_error(msg.str());
            }
        } else if(strcmp(name, "height") == 0) {
            if(sscanf(value, "%f", &height) != 1) {
                std::stringstream msg;
                msg << "Couldn't parse height attribute (" << value << ").";
                throw std::runtime_error(msg.str());
            }
        } else if(strcmp(name, "border") == 0) {
            if(sscanf(value, "%d", &border) != 1) {
                std::stringstream msg;
                msg << "Couldn't parse border attribute (" << value << ").";
                throw std::runtime_error(msg.str());
            }
        } else if(strcmp(name, "tilesize") == 0) {
            if(sscanf(value, "%d", &tilesize) != 1) {
                std::stringstream msg;
                msg << "Couldn't parse tilesize attribute (" << value << ").";
                throw std::runtime_error(msg.str());
            }
        } else {
            std::cerr << "Unknown attribute '" << name << "' skipped.\n";
        }
    }
    if(width <= 0 || height <= 0)
      throw std::runtime_error("Width or Height invalid");
    

    // create alpha-surface
    SDL_Surface* image = SDL_CreateRGBSurface(0, (int) width, (int) height, 32,
					      0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);

    SDL_Surface *copy = SDL_DisplayFormat(image);
    SDL_FreeSurface(image);
    image=copy;
    
    mTexture.reset(texture_manager->create(image)); // don't delete image, as Texture-class takes ownage

    mFullRefresh=true;
    alreadyAttached=false;
}

Component *MiniMap::findRoot(Component *c)
{
  while(c->getParent())
    return findRoot(c->getParent());
  return c;
}

void MiniMap::attachButtons()
{
    if(alreadyAttached)
        return;
    alreadyAttached=true;
  
    Component *root=findRoot(this);
  
    for(int i = 0; mapViewButtons[i] != 0; ++i) {
        CheckButton* b = getCheckButton(*root, mapViewButtons[i]);
        if(i == 0)
            b->check();
        b->clicked.connect(makeCallback(*this, &MiniMap::mapViewButtonClicked));
    }
    for(int i = 0; speedButtons[i] != 0; ++i) {
        CheckButton* b = getCheckButton(*root, speedButtons[i]);
        // we start in normal speed...
        if(i == 1)
            b->check();
        b->clicked.connect(makeCallback(*this, &MiniMap::speedButtonClicked));
    }

    Button* zoomInButton = getButton(*root, "ZoomInButton");
    zoomInButton->clicked.connect(makeCallback(*this, &MiniMap::zoomInButtonClicked));
    Button* zoomOutButton = getButton(*root, "ZoomOutButton");
    zoomOutButton->clicked.connect(makeCallback(*this, &MiniMap::zoomOutButtonClicked));
}

void MiniMap::mapViewButtonClicked(CheckButton* button, int)
{
    Component *root = findRoot(this);
    std::string name = button->getName();
    int i;
    for(i = 0; mapViewButtons[i] != 0; ++i) {
        if(name == mapViewButtons[i])
            break;
    }
    assert(mapViewButtons[i] != 0);

    for(int b = 0; mapViewButtons[b] != 0; ++b) {
        if(b != i) {
            CheckButton* button = getCheckButton(*root, mapViewButtons[b]);
            button->uncheck();
        } else {
            button->check();
        }
    }

    switch(i) {
        case 0: mMode=NORMAL;getGameView()->setMapMode(NORMAL);break;
        case 1: mMode=UB40;getGameView()->setMapMode(UB40);break;
        case 2: mMode=POLLUTION;getGameView()->setMapMode(POLLUTION);break;
        case 3: mMode=STARVE;getGameView()->setMapMode(STARVE);break;
        case 4: mMode=POWER;getGameView()->setMapMode(POWER);break;
        case 5: mMode=FIRE;getGameView()->setMapMode(FIRE);break;
        case 6: mMode=CRICKET;getGameView()->setMapMode(CRICKET);break;
        case 7: mMode=HEALTH;getGameView()->setMapMode(HEALTH);break;
        case 8: if(( coal_survey_done == 0 ) && ( !blockingDialogIsOpen )){
                    new Dialog( ASK_COAL_SURVEY ); 
                }
                mMode=COAL;
                getGameView()->setMapMode(COAL);
                break;
        default:
            assert(false);
    }
    
    mFullRefresh=true;
}

void
MiniMap::speedButtonClicked(CheckButton* button, int)
{
    Component *root = findRoot(this);
    std::string name = button->getName();
    int i;
    for(i = 0; speedButtons[i] != 0; ++i) {
        if(name == speedButtons[i])
            break;
    }
    assert(speedButtons[i] != 0);

    for(int b = 0; speedButtons[b] != 0; ++b) {
        if(b != i) {
            CheckButton* button = getCheckButton(*root, speedButtons[b]);
            button->uncheck();
        } else {
            button->check();
        }
    }

    switch(i) {
        case 0:
            setLincitySpeed(0);
            break;
        case 1:
            setLincitySpeed(SLOW_TIME_FOR_YEAR);
            break;
        case 2:
            setLincitySpeed(MED_TIME_FOR_YEAR);
            break;
        case 3:
            setLincitySpeed(FAST_TIME_FOR_YEAR);
            break;
        default:
            assert(false);
            break;
    }
}

void
MiniMap::zoomInButtonClicked(Button* )
{
    getGameView()->zoomIn();
}

void
MiniMap::zoomOutButtonClicked(Button* )
{
    getGameView()->zoomOut();
}

Vector2
MiniMap::mapPointToVector(MapPoint p)
{
    return Vector2(p.x * tilesize, p.y * tilesize);
}

/*
 *  Set the Corners of the GameView to show in Minimap
 */
void MiniMap::setGameViewCorners(const MapPoint& upperLeft,
        const MapPoint& upperRight, const MapPoint& lowerRight,
        const MapPoint& lowerLeft )
{
    gameViewPoints[ 0 ] = mapPointToVector(upperLeft);
    gameViewPoints[ 1 ] = mapPointToVector(upperRight);
    gameViewPoints[ 2 ] = mapPointToVector(lowerRight);
    gameViewPoints[ 3 ] = mapPointToVector(lowerLeft);
    mFullRefresh=true;
    setDirty();
}

void MiniMap::draw(Painter &painter)
{
  attachButtons();

  int x, y;
  short typ, grp;

  // simple and bad implementation
  // FIXME: should be stored SDL_Surface and then blitted
  // SDL_Surface should be updated, only if needed

  std::auto_ptr<Painter> mpainter (painter.createTexturePainter(mTexture.get()));
  Color alphawhite( 255, 255, 255, 128 );
  if(mpainter.get() == 0)
  {
    // workaround - so that it works with GL, too, as long as there's no TexturePainter for this
  
    for(y=0;y<WORLD_SIDE_LEN && y<height/tilesize;y++)
      for(x=0;x<WORLD_SIDE_LEN && x<width/tilesize;x++)
        {
          typ = MP_TYPE(x,y);
          if (typ != mappointoldtype[x][y] || mFullRefresh)
          {
            mappointoldtype[x][y] = typ;
            grp = get_group_of_type(typ);
  
            Color mc=getColor(x,y);
            painter.setFillColor(mc);
            painter.fillRectangle(Rect2D(x*tilesize,y*tilesize,(x+main_groups[grp].size)*tilesize+1,(y+main_groups[grp].size)*tilesize));
          }
        }
    //show current GameView
    painter.setLineColor( alphawhite );
    painter.drawPolygon( 4, gameViewPoints );    
    return;
  }

  for(y=0;y<WORLD_SIDE_LEN && y<height/tilesize;y++)
    for(x=0;x<WORLD_SIDE_LEN && x<width/tilesize;x++)
      {
        typ = MP_TYPE(x,y);
        if (typ != mappointoldtype[x][y] || mFullRefresh)
        {
          mappointoldtype[x][y] = typ;
          grp = get_group_of_type(typ);

          Color mc=getColor(x,y);
          mpainter->setFillColor(mc);
          mpainter->fillRectangle(Rect2D(x*tilesize,y*tilesize,(x+main_groups[grp].size)*tilesize+1,(y+main_groups[grp].size)*tilesize));
        }
      }
    //show current GameView
    mpainter->setLineColor( alphawhite );
    mpainter->drawPolygon( 4, gameViewPoints );    


  painter.drawTexture(mTexture.get(), Vector2(0, 0));

  mFullRefresh=false;
}

Color MiniMap::getColorNormal(int x,int y) const
{
  int xx,yy;
  
  xx=x;
  yy=y;
  if (MP_TYPE(x,y) == CST_USED)
    {
      xx = MP_INFO(x,y).int_1;
      yy = MP_INFO(x,y).int_2;
    }

  int mc = main_groups[MP_GROUP(xx,yy)].colour;
  int red = 0;
  int green = 0;
  int blue = 0;

  if( mc & 32 ) 
      red = 8 * ( mc & 31 );
  if( mc & 64 )
      green = 8 * ( mc & 31 );
  if( mc & 128 )
      blue = 8 * (mc & 31 );

  return Color( red, green, blue ); 
}

Color MiniMap::getColor(int x,int y) const
{
  int xx,yy;
  
  xx=x;
  yy=y;
  if (MP_TYPE(x,y) == CST_USED)
    {
      xx = MP_INFO(x,y).int_1;
      yy = MP_INFO(x,y).int_2;
    }

  int flags=MP_INFO(xx,yy).flags;

  switch(mMode)
    {
    case NORMAL:
      return getColorNormal(x,y);
    case POLLUTION:
      {
	static std::set<short> pset;
	short p=MP_POL(x,y);
	if(pset.find(p)==pset.end())
	  {
	    pset.insert(p);
	  }
	float v=p/600.0;
	if(v<0)
	  v=0;
	if(v>1)
	  v=1;
	Color mc((int) (0xFF*v), (int) (0xFF*(1-v)), 0);
	mc=light(mc,brightness(getColorNormal(x,y)));
	return mc;
      }
    case FIRE:
    case CRICKET:
    case HEALTH:
      if( ((flags&FLAG_FIRE_COVER) && mMode==FIRE) ||
	  ((flags&FLAG_CRICKET_COVER) && mMode==CRICKET) ||
	  ((flags&FLAG_HEALTH_COVER) && mMode==HEALTH))
	{
	  Color mc(0,0xFF,0);
	  mc=light(mc,brightness(getColorNormal(x,y)));
	  return mc;
	}
      else
	return makeGrey(getColorNormal(x,y));
    case UB40:
      {
	
	if (MP_GROUP_IS_RESIDENCE(xx,yy)) 
	  {
	    if (MP_INFO(xx,yy).int_1 < -20)
	      return Color(0xFF,0,0);
	    else if (MP_INFO(xx,yy).int_1 < 10)
	      return Color(0x7F,0,0);
	    else
	      return Color(0,0xFF,0);
	  } 
	else 
	  {
	    return makeGrey(getColorNormal(x,y));//AGColor(0,0x7F,0);
	    //	    col = green (14);
	  }
      }
    case COAL:
      {
	Color c(0x77,0,0);
    if( coal_survey_done == 0 ){
        return Color(0,0,0);
    }
	if(MP_INFO(x,y).coal_reserve==0)
	  return makeGrey(getColorNormal(x,y));
	else if (MP_INFO(x,y).coal_reserve >= COAL_RESERVE_SIZE / 2)
	  return Color(0,0xFF,0);
	else if (MP_INFO(x,y).coal_reserve < COAL_RESERVE_SIZE / 2)
	  return Color(0xFF,0,0);

	return c;
      }
    case STARVE:
      {
	if (MP_GROUP_IS_RESIDENCE(xx,yy)) {
	  if ((total_time - MP_INFO(x,y).int_2) < 20)
	    return Color(0xFF,0,0);
	  else if ((total_time - MP_INFO(x,y).int_2) < 100)
	      return Color(0x7F,0,0);
	  else
	      return Color(0,0xFF,0);
	} else {
	  return makeGrey(getColorNormal(x,y));
	}
      }
    case POWER:
      {
	Color mc;
	if (get_power (xx, yy, 1, 1) != 0) {
	  mc=Color(0,0xFF,0);
	} else if (get_power (xx, yy, 1, 0) != 0) {
	  mc=Color(0,0x7F,0);
	} else {
	  mc=Color(0xFF,0xFF,0xFF);
	}
	  mc=light(mc,brightness(getColorNormal(x,y)));
	return mc;
      }
    case MAX:
      std::cerr<<"Undefined MiniMap-Display-type!"<<std::endl;
    };
  return Color(0xFF,0,0xFF);
}

void MiniMap::event(const Event& event)
{
    if(event.type==Event::MOUSEBUTTONDOWN && event.inside){
        // get Tile, that was clicked
        MapPoint tile (
                (int) ((event.mousepos.x - border ) / tilesize),
                (int) ((event.mousepos.y - border ) / tilesize));
        
        if(event.mousebutton == SDL_BUTTON_RIGHT ){ //right mouse
            getMPS()->setView( tile, MPS_GLOBAL );//show global info
            //cycle through global styles
            mps_global_style++;
            if( mps_global_style >= MPS_GLOBAL_STYLES )
                mps_global_style = 0;
            return;
        }

        // move main-map
        getGameView()->show(tile);
        mFullRefresh=true;
    }
}

IMPLEMENT_COMPONENT_FACTORY(MiniMap)

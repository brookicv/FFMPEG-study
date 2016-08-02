# include <iostream>
# include <SDL.h>

using namespace std;

int main(int argc, char *argv[])
{
	SDL_Window* pWindow = nullptr;
	SDL_Renderer* pRender = nullptr;

	// 1. Initialize SDL
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
	{
		cout << "SDL initialize failed:" << SDL_GetError() << endl;
		return 1;
	}

	// 2. Create window
	pWindow = SDL_CreateWindow("SDL Winwow", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		800, 640, SDL_WINDOW_SHOWN);

	if (!pWindow)
	{
		cout << "Create window failed:" << SDL_GetError() << endl;
		return 1;
	}

	// 3.Create render
	pRender = SDL_CreateRenderer(pWindow, -1, 0);

	// 4.Set back color to green
	SDL_SetRenderDrawColor(pRender, 255, 255, 255, 0);
	SDL_RenderClear(pRender);

	// 5. Show the window
	SDL_RenderPresent(pRender);

	SDL_Delay(5000);

	SDL_Quit();

	getchar();
	return 0;
}
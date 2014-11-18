#include "Player.h"
#include <sdl\SDL.h>

Player::Player(void):
	currentGunIndex(-1)
{
}

Player::~Player(void)
{
}

void Player::Init(int speed, glm::vec2 pos, Engine::InputManager *inputManager, Engine::Camera2D *camera, std::vector<Bullet> *bullets)
{
	this->speed = speed;
	this->position = pos;
	this->inputManager = inputManager;
	this->camera = camera;
	this->bullets = bullets;
	camera = camera;
	color.r = 0;
	color.b = 185;
	color.g = 0;
	health = 150;
}

void Player::AddGun(Gun *gun)
{
	guns.push_back(gun);

	// no gun equipped, equip gun.
	if(currentGunIndex == -1)
		currentGunIndex=0;
}

void Player::Update(const std::vector<std::string> &levelData, std::vector<Human*> &humans, std::vector<Zombie*> &zombies)
{
	if(inputManager->IsKeyPressed(SDLK_w))
		position.y += speed;
	else if(inputManager->IsKeyPressed(SDLK_s))
		position.y -= speed;
	if(inputManager->IsKeyPressed(SDLK_a))
		position.x -= speed;
	else if(inputManager->IsKeyPressed(SDLK_d))
		position.x += speed;

	if(inputManager->IsKeyPressed(SDLK_1) && guns.size() >= 0)
		currentGunIndex = 0;
	else if(inputManager->IsKeyPressed(SDLK_2) && guns.size() >= 1)
		currentGunIndex = 1;
	else if(inputManager->IsKeyPressed(SDLK_3) && guns.size() >= 2)
		currentGunIndex = 2;

	if(currentGunIndex != -1)
	{
		glm::vec2 mouseCoords = camera->ConvertScreenToWorld(inputManager->GetMouseCoords());
		glm::vec2 centerPosition = position + glm::vec2(AGENT_RADIUS);
		glm::vec2 direction = glm::normalize(mouseCoords - centerPosition);
		guns[currentGunIndex]->Update(inputManager->IsKeyPressed(SDL_BUTTON_LEFT), centerPosition, direction, *bullets);
	}

	CollideWithLevel(levelData);
}
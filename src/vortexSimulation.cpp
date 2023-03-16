#include <SFML/Window/Keyboard.hpp>
#include <ios>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <chrono>
#include "cartesian_grid_of_speed.hpp"
#include "vortex.hpp"
#include "cloud_of_points.hpp"
#include "runge_kutta.hpp"
#include "screen.hpp"

#include <mpi/mpi.h>

auto readConfigFile( std::ifstream& input )
{
    using point=Simulation::Vortices::point;

    int isMobile;
    std::size_t nbVortices;
    Numeric::CartesianGridOfSpeed cartesianGrid;
    Geometry::CloudOfPoints cloudOfPoints;
    constexpr std::size_t maxBuffer = 8192;
    char buffer[maxBuffer];
    std::string sbuffer;
    std::stringstream ibuffer;
    // Lit la première ligne de commentaire :
    input.getline(buffer, maxBuffer); // Relit un commentaire
    input.getline(buffer, maxBuffer);// Lecture de la grille cartésienne
    sbuffer = std::string(buffer,maxBuffer);
    ibuffer = std::stringstream(sbuffer);
    double xleft, ybot, h;
    std::size_t nx, ny;
    ibuffer >> xleft >> ybot >> nx >> ny >> h;
    cartesianGrid = Numeric::CartesianGridOfSpeed({nx,ny}, point{xleft,ybot}, h);
    input.getline(buffer, maxBuffer); // Relit un commentaire
    input.getline(buffer, maxBuffer); // Lit mode de génération des particules
    sbuffer = std::string(buffer,maxBuffer);
    ibuffer = std::stringstream(sbuffer);
    int modeGeneration;
    ibuffer >> modeGeneration;
    if (modeGeneration == 0) // Génération sur toute la grille 
    {
        std::size_t nbPoints;
        ibuffer >> nbPoints;
        cloudOfPoints = Geometry::generatePointsIn(nbPoints, {cartesianGrid.getLeftBottomVertex(), cartesianGrid.getRightTopVertex()});
    }
    else 
    {
        std::size_t nbPoints;
        double xl, xr, yb, yt;
        ibuffer >> xl >> yb >> xr >> yt >> nbPoints;
        cloudOfPoints = Geometry::generatePointsIn(nbPoints, {point{xl,yb}, point{xr,yt}});
    }
    // Lit le nombre de vortex :
    input.getline(buffer, maxBuffer); // Relit un commentaire
    input.getline(buffer, maxBuffer); // Lit le nombre de vortex
    sbuffer = std::string(buffer, maxBuffer);
    ibuffer = std::stringstream(sbuffer);
    try {
        ibuffer >> nbVortices;        
    } catch(std::ios_base::failure& err)
    {
        std::cout << "Error " << err.what() << " found" << std::endl;
        std::cout << "Read line : " << sbuffer << std::endl;
        throw err;
    }
    Simulation::Vortices vortices(nbVortices, {cartesianGrid.getLeftBottomVertex(),
                                               cartesianGrid.getRightTopVertex()});
    input.getline(buffer, maxBuffer);// Relit un commentaire
    for (std::size_t iVortex=0; iVortex<nbVortices; ++iVortex)
    {
        input.getline(buffer, maxBuffer);
        double x,y,force;
        std::string sbuffer(buffer, maxBuffer);
        std::stringstream ibuffer(sbuffer);
        ibuffer >> x >> y >> force;
        vortices.setVortex(iVortex, point{x,y}, force);
    }
    input.getline(buffer, maxBuffer);// Relit un commentaire
    input.getline(buffer, maxBuffer);// Lit le mode de déplacement des vortex
    sbuffer = std::string(buffer,maxBuffer);
    ibuffer = std::stringstream(sbuffer);
    ibuffer >> isMobile;
    return std::make_tuple(vortices, isMobile, cartesianGrid, cloudOfPoints);
}


int main(int argc, char* argv[] )
{
    MPI_Comm global;
    int rank, nbp;

    MPI_Init(&argc, &argv);
    MPI_Comm_dup(MPI_COMM_WORLD, &global);
    MPI_Comm_size(global, &nbp);
    MPI_Comm_rank(global, &rank);

    MPI_Request request = MPI_REQUEST_NULL;
    MPI_Status status;

    char const *filename;
    if (argc == 1) {
        std::cout << "Usage : vortexsimulator <nom fichier configuration>" << std::endl;
        return EXIT_FAILURE;
    }

    filename = argv[1];
    std::ifstream fich(filename);
    auto config = readConfigFile(fich);
    fich.close();

    std::size_t resx = 800, resy = 600;
    if (argc > 3) {
        resx = std::stoull(argv[2]);
        resy = std::stoull(argv[3]);
    }

    auto vortices = std::get<0>(config);
    auto isMobile = std::get<1>(config);
    auto grid = std::get<2>(config);
    auto cloud = std::get<3>(config);

    bool running = true;
    bool animate = false;
    double dt = 0.1;

    if (rank == 0)
    {
        std::cout << "######## Vortex simultor ########" << std::endl << std::endl;
        std::cout << "Press P for play animation " << std::endl;
        std::cout << "Press S to stop animation" << std::endl;
        std::cout << "Press right cursor to advance step by step in time" << std::endl;
        std::cout << "Press down cursor to halve the time step" << std::endl;
        std::cout << "Press up cursor to double the time step" << std::endl;

        grid.updateVelocityField(vortices);

        Graphisme::Screen myScreen({resx, resy}, {grid.getLeftBottomVertex(), grid.getRightTopVertex()});

        while (running) {
            auto start = std::chrono::system_clock::now();

            bool advance = false;

            // on inspecte tous les évènements de la fenêtre qui ont été émis depuis la précédente itération
            sf::Event event;
            while (myScreen.pollEvent(event)) {
                bool shouldSend = false;

                // évènement "fermeture demandée" : on ferme la fenêtre
                if (event.type == sf::Event::Closed) {
                    running = false;
                    shouldSend = true;
                }

                if (event.type == sf::Event::Resized) {
                    // on met à jour la vue, avec la nouvelle taille de la fenêtre
                    myScreen.resize(event);
                }

                if (sf::Keyboard::isKeyPressed(sf::Keyboard::P) && !animate) {
                    animate = true;
                    shouldSend = true;
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::S) && animate) {
                    animate = false;
                    shouldSend = true;
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Up)) {
                    dt *= 2;
                    shouldSend = true;
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Down)) {
                    dt /= 2;
                    shouldSend = true;
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right) && !advance) {
                    advance = true;
                    shouldSend = true;
                }

                if (shouldSend)
                {
                    std::cout << "[0] Sending" << std::endl;
                    MPI_Isend(&animate, 1, MPI_CXX_BOOL, 1, 0, global, &request);
                    MPI_Isend(&advance, 1, MPI_CXX_BOOL, 1, 0, global, &request);
                    MPI_Isend(&running, 1, MPI_CXX_BOOL, 1, 0, global, &request);
                    MPI_Isend(&dt, 1, MPI_DOUBLE, 1, 0, global, &request);
                }

                if (!running)
                {
                    myScreen.close();
                    break;
                }
            }
            myScreen.clear(sf::Color::Black);
            std::string strDt = std::string("Time step : ") + std::to_string(dt);
            myScreen.drawText(strDt, Geometry::Point<double>{50, double(myScreen.getGeometry().second - 96)});
            myScreen.displayVelocityField(grid, vortices);
            myScreen.displayParticles(grid, vortices, cloud);

            auto beginning = std::chrono::system_clock::now();
            if (animate | advance) {
                if (isMobile) {
                    MPI_Recv(grid.data(), grid.mpi_size(), MPI_DOUBLE, 1, 0, global, &status);
                    MPI_Recv(vortices.data(), vortices.mpi_size(), MPI_DOUBLE, 1, 0, global, &status);
                    MPI_Recv(cloud.data(), cloud.mpi_size(), MPI_DOUBLE, 1, 0, global, &status);
                } else {
                    MPI_Recv(grid.data(), grid.mpi_size(), MPI_DOUBLE, 1, 0, global, &status);
                    MPI_Recv(cloud.data(), cloud.mpi_size(), MPI_DOUBLE, 1, 0, global, &status);
                }
            }
            std::chrono::duration<double> recvDuration = std::chrono::system_clock::now() - beginning;

            auto end = std::chrono::system_clock::now();
            std::chrono::duration<double> diff = end - start;

            if (animate | advance) {
                std::cout << "[0] Took " << diff.count() << "\t" << recvDuration.count() * 100 / diff.count() << "%" << std::endl;
            }

            std::string str_fps = std::string("FPS : ") + std::to_string(1. / diff.count());
            myScreen.drawText(str_fps, Geometry::Point<double>{300, double(myScreen.getGeometry().second - 96)});
            myScreen.display();
        }
    }
    else if (rank == 1)
    {
        std::chrono::duration<double> calcDura{};
        std::chrono::duration<double> calcAndSendDura{};

        while (running) {
            auto start = std::chrono::system_clock::now();

            bool advance = false;

            int flag = 0;
            MPI_Iprobe(0, 0, global, &flag, &status);
            if (flag)
            {
                std::cout << "[1] Reading" << std::endl;
                MPI_Recv(&animate, 1, MPI_CXX_BOOL, 0, 0, global, &status);
                MPI_Recv(&advance, 1, MPI_CXX_BOOL, 0, 0, global, &status);
                MPI_Recv(&running, 1, MPI_CXX_BOOL, 0, 0, global, &status);
                MPI_Recv(&dt, 1, MPI_DOUBLE, 0, 0, global, &status);

                if (!running)
                    break;
            }

            if (animate | advance) {
                if (isMobile) {
                    auto beginning = std::chrono::system_clock::now();
                    cloud = Numeric::solve_RK4_movable_vortices(dt, grid, vortices, cloud);
                    calcDura = std::chrono::system_clock::now() - beginning;

                    MPI_Send(grid.data(), grid.mpi_size(), MPI_DOUBLE, 0, 0, global);
                    MPI_Send(vortices.data(), vortices.mpi_size(), MPI_DOUBLE, 0, 0, global);
                    MPI_Send(cloud.data(), cloud.mpi_size(), MPI_DOUBLE, 0, 0, global);

                    calcAndSendDura = std::chrono::system_clock::now() - beginning;
                } else {
                    auto beginning = std::chrono::system_clock::now();
                    cloud = Numeric::solve_RK4_fixed_vortices(dt, grid, cloud);
                    calcDura = std::chrono::system_clock::now() - beginning;

                    MPI_Send(grid.data(), grid.mpi_size(), MPI_DOUBLE, 0, 0, global);
                    MPI_Send(cloud.data(), cloud.mpi_size(), MPI_DOUBLE, 0, 0, global);

                    calcAndSendDura = std::chrono::system_clock::now() - beginning;
                }
            }
            auto end = std::chrono::system_clock::now();
            std::chrono::duration<double> diff = end - start;

            std::cout << "[1] Took " << diff.count() << "\t" << (calcAndSendDura.count() - calcDura.count()) * 100/diff.count() << "%" << std::endl;
        }
    }


    MPI_Finalize();
    return EXIT_SUCCESS;
 }
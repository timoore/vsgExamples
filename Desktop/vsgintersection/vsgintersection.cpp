#include <vsg/all.h>

#ifdef USE_VSGXCHANGE
#include <vsgXchange/ReaderWriter_all.h>
#include <vsgXchange/ShaderCompiler.h>
#endif

#include <iostream>
#include <chrono>
#include <thread>

class IntersectonHandler : public vsg::Inherit<vsg::Visitor, IntersectonHandler>
{
public:

    IntersectonHandler() {}

    void apply(vsg::KeyPressEvent& keyPress) override
    {
        std::cout<<"KeyPress "<<keyPress.keyBase<<", "<<keyPress.keyModified<<std::endl;
    }

    void apply(vsg::ButtonPressEvent& buttonPressEvent) override
    {
        std::cout<<"ButtonPressEvent z = "<<buttonPressEvent.x<<", y = "<<buttonPressEvent.y<<", button = "<<buttonPressEvent.button<<std::endl;
    }

    void apply(vsg::MoveEvent& moveEvent) override
    {
        std::cout<<"MoveEvent x = "<<moveEvent.x<<", y = "<<moveEvent.y<<", mask = "<<moveEvent.mask<<std::endl;
    }
};



int main(int argc, char** argv)
{
    // set up defaults and read command line arguments to override them
    auto options = vsg::Options::create();
    auto windowTraits = vsg::WindowTraits::create();
    windowTraits->windowTitle = "vsginteresction";

    // set up defaults and read command line arguments to override them
    vsg::CommandLine arguments(&argc, argv);
    windowTraits->debugLayer = arguments.read({"--debug","-d"});
    windowTraits->apiDumpLayer = arguments.read({"--api","-a"});
    if (arguments.read({"--fullscreen", "--fs"})) windowTraits->fullscreen = true;
    if (arguments.read({"--window", "-w"}, windowTraits->width, windowTraits->height)) { windowTraits->fullscreen = false; }
    auto pointOfInterest = arguments.value(vsg::dvec3(0.0, 0.0, std::numeric_limits<double>::max()), "--poi");
    auto horizonMountainHeight = arguments.value(-1.0, "--hmh");
    auto useDatabasePager = arguments.read("--pager");
    auto maxPageLOD = arguments.value(-1, "--max-plod");

    if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

#ifdef USE_VSGXCHANGE
    // add use of vsgXchange's support for reading and writing 3rd party file formats
    options->readerWriter = vsgXchange::ReaderWriter_all::create();
#endif

    using VsgNodes = std::vector<vsg::ref_ptr<vsg::Node>>;
    VsgNodes vsgNodes;

    vsg::Path path;

    // read any vsg files
    for (int i=1; i<argc; ++i)
    {
        vsg::Path filename = arguments[i];

        path = vsg::filePath(filename);

        auto loaded_scene = vsg::read_cast<vsg::Node>(filename, options);
        if (loaded_scene)
        {
            vsgNodes.push_back(loaded_scene);
            arguments.remove(i, 1);
            --i;
        }
    }

    // assign the vsg_scene from the loaded nodes
    vsg::ref_ptr<vsg::Node> vsg_scene;
    if (vsgNodes.size()>1)
    {
        auto vsg_group = vsg::Group::create();
        for(auto& subgraphs : vsgNodes)
        {
            vsg_group->addChild(subgraphs);
        }

        vsg_scene = vsg_group;
    }
    else if (vsgNodes.size()==1)
    {
        vsg_scene = vsgNodes.front();
    }


    if (!vsg_scene)
    {
        std::cout<<"No command graph created."<<std::endl;
        return 1;
    }


    // create the viewer and assign window(s) to it
    auto viewer = vsg::Viewer::create();

    vsg::ref_ptr<vsg::Window> window(vsg::Window::create(windowTraits));
    if (!window)
    {
        std::cout<<"Could not create windows."<<std::endl;
        return 1;
    }

    viewer->addWindow(window);


    auto ellipsoidModel = vsg::EllipsoidModel::create();

    vsg::ref_ptr<vsg::LookAt> lookAt;

    // compute the bounds of the scene graph to help position camera
    vsg::ComputeBounds computeBounds;
    vsg_scene->accept(computeBounds);
    vsg::dvec3 centre = (computeBounds.bounds.min+computeBounds.bounds.max)*0.5;
    double radius = vsg::length(computeBounds.bounds.max-computeBounds.bounds.min)*0.6;

    if (pointOfInterest[2] != std::numeric_limits<double>::max())
    {
        auto ellipsoidModel = vsg::EllipsoidModel::create();
        auto ecef = ellipsoidModel->convertLatLongHeightToECEF({vsg::radians(pointOfInterest[0]), vsg::radians(pointOfInterest[1]), 0.0});
        auto ecef_normal = vsg::normalize(ecef);

        vsg::dvec3 centre = ecef;
        vsg::dvec3 eye = centre + ecef_normal * pointOfInterest[2];
        vsg::dvec3 up = vsg::normalize( vsg::cross(ecef_normal, vsg::cross(vsg::dvec3(0.0, 0.0, 1.0), ecef_normal) ) );

        // set up the camera
        lookAt = vsg::LookAt::create(eye, centre, up);
    }
    else
    {
        // set up the camera
        lookAt = vsg::LookAt::create(centre+vsg::dvec3(0.0, -radius*3.5, 0.0), centre, vsg::dvec3(0.0, 0.0, 1.0));
    }

    double nearFarRatio = 0.0001;
    vsg::ref_ptr<vsg::ProjectionMatrix> perspective;
    if (horizonMountainHeight >= 0.0)
    {
        perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio, horizonMountainHeight);
    }
    else
    {
        perspective = vsg::Perspective::create(30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio*radius, radius * 4.5);
    }

    auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

    // set up database pager
    vsg::ref_ptr<vsg::DatabasePager> databasePager;
    if (useDatabasePager)
    {
        databasePager = vsg::DatabasePager::create();
        if (maxPageLOD>=0) databasePager->targetMaxNumPagedLODWithHighResSubgraphs = maxPageLOD;
    }

    // add close handler to respond the close window button and pressing escape
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    viewer->addEventHandler(vsg::Trackball::create(camera));

    viewer->addEventHandler(IntersectonHandler::create());

    auto commandGraph = vsg::createCommandGraphForView(window, camera, vsg_scene);
    viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph}, databasePager);

    viewer->compile();

    // rendering main loop
    while (viewer->advanceToNextFrame())
    {
        // pass any events into EventHandlers assigned to the Viewer
        viewer->handleEvents();

        viewer->update();

        viewer->recordAndSubmit();

        viewer->present();
    }

    // clean up done automatically thanks to ref_ptr<>
    return 0;
}
/**
 * Copyright (C) 2017 Universität Osnabrück
 * This file is part of the LAS VEGAS Reconstruction Toolkit,
 *
 * LAS VEGAS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * LAS VEGAS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 */


// Program options for this tool

#include <lvr/io/Timestamp.hpp>
#include <lvr/io/ModelFactory.hpp>
#include <lvr/reconstruction/ModelToImage.hpp>
#include <lvr/reconstruction/PanoramaNormals.hpp>
#include "Options.hpp"

#include <boost/filesystem.hpp>

#include <Eigen/Dense>

#include <fstream>
using std::ifstream;

using namespace lvr;

Eigen::Matrix4d buildTransformation(double* alignxf)
{
    Eigen::Matrix3d rotation;
    Eigen::Vector4d translation;

    rotation  << alignxf[0],  alignxf[4],  alignxf[8],
    alignxf[1],  alignxf[5],  alignxf[9],
    alignxf[2],  alignxf[6],  alignxf[10];

    translation << alignxf[12], alignxf[13], alignxf[14], 1.0;

    Eigen::Matrix4d transformation;
    transformation.setIdentity();
    transformation.block<3,3>(0,0) = rotation;
    transformation.rightCols<1>() = translation;

    return transformation;
}

Eigen::Matrix4d getTransformationFromPose(boost::filesystem::path& pose)
{
    ifstream poseIn(pose.c_str());
    if(poseIn.good())
    {
        double rPosTheta[3];
        double rPos[3];
        double alignxf[16];

        poseIn >> rPos[0] >> rPos[1] >> rPos[2];
        poseIn >> rPosTheta[0] >> rPosTheta[1] >> rPosTheta[2];

        rPosTheta[0] *= 0.0174533;
        rPosTheta[1] *= 0.0174533;
        rPosTheta[2] *= 0.0174533;

        double sx = sin(rPosTheta[0]);
        double cx = cos(rPosTheta[0]);
        double sy = sin(rPosTheta[1]);
        double cy = cos(rPosTheta[1]);
        double sz = sin(rPosTheta[2]);
        double cz = cos(rPosTheta[2]);

        alignxf[0]  = cy*cz;
        alignxf[1]  = sx*sy*cz + cx*sz;
        alignxf[2]  = -cx*sy*cz + sx*sz;
        alignxf[3]  = 0.0;
        alignxf[4]  = -cy*sz;
        alignxf[5]  = -sx*sy*sz + cx*cz;
        alignxf[6]  = cx*sy*sz + sx*cz;
        alignxf[7]  = 0.0;
        alignxf[8]  = sy;
        alignxf[9]  = -sx*cy;
        alignxf[10] = cx*cy;

        alignxf[11] = 0.0;

        alignxf[12] = rPos[0];
        alignxf[13] = rPos[1];
        alignxf[14] = rPos[2];
        alignxf[15] = 1;

        return buildTransformation(alignxf);
    }
    else
    {
        return Eigen::Matrix4d::Identity();
    }
}

Eigen::Matrix4d getTransformationFromFrames(boost::filesystem::path& frames)
{
    double alignxf[16];
    int color;

    std::ifstream in(frames.c_str());
    int c = 0;
    while(in.good())
    {
        c++;
        for(int i = 0; i < 16; i++)
        {
            in >> alignxf[i];
        }

        in >> color;

        if(!in.good())
        {
            c = 0;
            break;
        }
    }

    return buildTransformation(alignxf);
}

void computeNormals(string filename, image_normals::Options& opt, PointBufferPtr& buffer)
{
    ModelPtr model = ModelFactory::readModel(filename);

    // Determine coordinate system
    ModelToImage::CoordinateSystem system = ModelToImage::NATIVE;

    if(opt.coordinateSystem() == "SLAM6D")
    {
        system = ModelToImage::SLAM6D;
    }
    else if(opt.coordinateSystem() == "UOS")
    {
        system = ModelToImage::UOS;
    }

    ModelToImage mti(
                model->m_pointCloud,
                ModelToImage::CYLINDRICAL,
                opt.imageWidth(), opt.imageHeight(),
                opt.minZ(), opt.maxZ(),
                opt.minH(), opt.maxH(),
                opt.minV(), opt.maxV(),
                opt.optimize(), system);

    mti.writePGM(opt.imageFile(), 3000);

    PanoramaNormals normals(&mti);
    buffer = normals.computeNormals(opt.regionWidth(), opt.regionHeight(), false);
}

void transform(PointBufferPtr& buffer, boost::filesystem::path& inFile, vector<float>& pts, vector<float>& nrm)
{
     cout << timestamp << "Transforming normals " << endl;

     char frames[2048];
     char pose[2014];

     sprintf(frames, "%s/%s.frames", inFile.parent_path().c_str(), inFile.stem().c_str());
     sprintf(pose, "%s/%s.pose", inFile.parent_path().c_str(), inFile.stem().c_str());

     boost::filesystem::path framesPath(frames);
     boost::filesystem::path posePath(pose);


     Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();

     if(boost::filesystem::exists(framesPath))
     {
        cout << timestamp << "Transforming according to " << framesPath.filename() << endl;
        transform = getTransformationFromFrames(framesPath);
     }
     else if(boost::filesystem::exists(posePath))
     {
        cout << timestamp << "Transforming according to " << posePath.filename() << endl;
        transform = getTransformationFromFrames(posePath);
     }
     else
     {
        cout << timestamp << "Warning: found no transformation for " << inFile.filename() << endl;
     }

     size_t n_normals;
     size_t n_points;

     floatArr normals = buffer->getPointNormalArray(n_normals);
     floatArr points = buffer->getPointArray(n_points);

     if(n_normals != n_points)
     {
         cout << timestamp << "Warning: point and normal count mismatch" << endl;
         return;
     }

     for(size_t i = 0; i < n_points; i++)
     {

        float x = points[3 * i];
        float y = points[3 * i + 1];
        float z = points[3 * i + 2];

        Eigen::Vector4d v(x,y,z,1);
        Eigen::Vector4d tv = transform * v;

//        points[3 * i]     = tv[0];
//        points[3 * i + 1] = tv[1];
//        points[3 * i + 2] = tv[2];

        pts.push_back(tv[0]);
        pts.push_back(tv[1]);
        pts.push_back(tv[2]);

        Eigen::Matrix3d rotation = transform.block(0, 0, 3, 3);

        float nx = normals[3 * i];
        float ny = normals[3 * i + 1];
        float nz = normals[3 * i + 2];

        Eigen::Vector3d normal(nx, ny, nz);
        Eigen::Vector3d tn = rotation * normal;

//        normals[3 * i]     = tn[0];
//        normals[3 * i + 1] = tn[1];
//        normals[3 * i + 2] = tn[2];

        nrm.push_back(tn[0]);
        nrm.push_back(tn[1]);
        nrm.push_back(tn[2]);
     }

}

void writeVectors(vector<float>& p, vector<float>& n, string outfile)
{

    ModelPtr model(new Model);
    PointBufferPtr buffer(new PointBuffer);
    
    // Passing the raw data pointers from the vectors
    // to a shared array is a bad idea. Due to the PointBuffer
    // interface we have to copy the data :-(
//    floatArr points(p.data());
//    floatArr normals(n.data());

    floatArr points(new float[p.size()]);
    floatArr normals(new float[n.size()]);

    cout << timestamp << "Copying buffers for output." << endl;
    // Assuming p and n have the same size (which they should)
    for(size_t i = 0; i < p.size(); i++)
    {
        points[i] = p[i];
        normals[i] = n[i];
    }

    buffer->setPointArray(points, p.size() / 3);
    buffer->setPointNormalArray(normals, n.size() / 3);

    model->m_pointCloud = buffer;

    cout << timestamp << "Saving " << outfile << endl;
    ModelFactory::saveModel(model, outfile);
    cout << timestamp << "Done." << endl;
}

/**
 * @brief   Main entry point for the LSSR surface executable
 */
int main(int argc, char** argv)
{
    image_normals::Options opt(argc, argv);
    cout << opt << endl;
    
    boost::filesystem::path inFile(opt.inputFile());

    if(boost::filesystem::is_directory(inFile))
    {
        vector<float> all_points;
        vector<float> all_normals;

        boost::filesystem::directory_iterator lastFile;
        for(boost::filesystem::directory_iterator it(inFile); it != lastFile; it++ )
        {
            boost::filesystem::path p = boost::filesystem::canonical(it->path());
            string currentFile = p.filename().string();

            if(string(p.extension().string().c_str()) == ".3d")
            {
                // Check for naming convention "scanxxx.3d"
                int num = 0;
                if(sscanf(currentFile.c_str(), "scan%3d", &num))
                {
                    cout << timestamp << "Processing " << p.string() << endl;
                    PointBufferPtr buffer;
                    computeNormals(p.string(), opt, buffer);
                    transform(buffer, p, all_points, all_normals);

                }
            }
        }

        writeVectors(all_points, all_normals, opt.outputFile());
    }
    else
    {
        PointBufferPtr buffer;
        computeNormals(opt.inputFile(), opt, buffer);
        ModelPtr out_model(new Model(buffer));
        ModelFactory::saveModel(out_model, opt.outputFile());
    }

}


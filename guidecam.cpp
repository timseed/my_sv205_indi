// indi_sv205.cpp - INDI-compatible guide camera driver for SV205 using V4L2

#include <libindi/defaultdevice.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

class GuideCam : public INDI::DefaultDevice
{
public:
    GuideCam()
    {
        setDeviceName("SV205GuideCam");
    }

    ~GuideCam() override
    {
        if (video_fd >= 0)
            close(video_fd);
    }

    const char *getDefaultName() override
    {
        return "SV205GuideCam";
    }

protected:
    bool Connect() override
    {
        video_fd = open("/dev/video0", O_RDWR);
        if (video_fd < 0)
        {
            LOG_ERROR("Cannot open /dev/video0.");
            return false;
        }
        LOG_INFO("SV205 camera connected.");
        return true;
    }

    bool Disconnect() override
    {
        if (video_fd >= 0)
            close(video_fd);
        video_fd = -1;
        LOG_INFO("Camera disconnected.");
        return true;
    }

    bool initProperties() override
    {
        INDI::DefaultDevice::initProperties();

        IUFillNumber(&ExposureN[0], "GUIDER_EXPOSURE_VALUE", "Duration (s)", "%5.2f", 0.001, 10, 0.1, 1);
        IUFillNumberVector(&ExposureNP, ExposureN, 1, getDeviceName(), "GUIDER_EXPOSURE", "Exposure", "Main Control", IP_RW, 60, IPS_IDLE);
        registerProperty(&ExposureNP);

        IUFillBLOB(&ImageB, "GUIDER_IMAGE", "Image", "");
        IUFillBLOBVector(&ImageBP, &ImageB, 1, getDeviceName(), "CCD1", "Guider Image", "Main Control", IP_RO, 60, IPS_IDLE);
        registerProperty(&ImageBP);

        return true;
    }

    bool updateProperties() override
    {
        INDI::DefaultDevice::updateProperties();
        if (isConnected())
        {
            defineProperty(&ExposureNP);
            defineProperty(&ImageBP);
        }
        else
        {
            deleteProperty(ExposureNP.name);
            deleteProperty(ImageBP.name);
        }
        return true;
    }
virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override
{
    if (strcmp(dev, getDeviceName()) == 0 && strcmp(name, ExposureNP.name) == 0)
    {
        double duration = values[0];
        ExposureNP.s = IPS_BUSY;
        IDSetNumber(&ExposureNP, nullptr);
        LOGF_INFO("Exposing for %.2f seconds...", duration);
        usleep(duration * 1e6);

        if (captureFrame())
            ExposureNP.s = IPS_OK;
        else
            ExposureNP.s = IPS_ALERT;

        IDSetNumber(&ExposureNP, nullptr);
        return true;
    }

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}



private:
    int video_fd = -1;
    INumber ExposureN[1];
    INumberVectorProperty ExposureNP;

    IBLOB ImageB;
    IBLOBVectorProperty ImageBP;

    bool captureFrame()
    {
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(video_fd, VIDIOC_S_FMT, &fmt) < 0)
        {
            LOG_ERROR("Failed to set format.");
            return false;
        }

        struct v4l2_requestbuffers req = {};
        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(video_fd, VIDIOC_REQBUFS, &req) < 0)
        {
            LOG_ERROR("Failed to request buffer.");
            return false;
        }

        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = 0;
        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            LOG_ERROR("Failed to query buffer.");
            return false;
        }

        void *buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset);
        if (buffer == MAP_FAILED)
        {
            LOG_ERROR("Failed to mmap buffer.");
            return false;
        }

        if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0)
        {
            LOG_ERROR("Failed to queue buffer.");
            munmap(buffer, buf.length);
            return false;
        }

        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(video_fd, VIDIOC_STREAMON, &type) < 0)
        {
            LOG_ERROR("Failed to start streaming.");
            munmap(buffer, buf.length);
            return false;
        }

        if (ioctl(video_fd, VIDIOC_DQBUF, &buf) < 0)
        {
            LOG_ERROR("Failed to dequeue buffer.");
            munmap(buffer, buf.length);
            return false;
        }

        ImageB.blob = (uint8_t *)malloc(buf.bytesused);
        memcpy(ImageB.blob, buffer, buf.bytesused);
        ImageB.bloblen = buf.bytesused;
        strncpy(ImageB.format, "image/jpeg", sizeof(ImageB.format));

        strncpy(ImageB.name, "SV205.jpg", MAXINDIBLOBFMT);

        ImageBP.s = IPS_OK;
        IDSetBLOB(&ImageBP, nullptr);

        free(ImageB.blob);

        munmap(buffer, buf.length);
        ioctl(video_fd, VIDIOC_STREAMOFF, &type);

        return true;
    }
};

extern "C" {
    GuideCam *create_device() {
        return new GuideCam();
    }
}


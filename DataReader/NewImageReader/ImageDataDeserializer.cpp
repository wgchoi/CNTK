#include "stdafx.h"
#include "ImageDataDeserializer.h"
#include <opencv2/opencv.hpp>

namespace Microsoft { namespace MSR { namespace CNTK {

    ImageDataDeserializer::ImageDataDeserializer(ImageConfigHelperPtr configHelper, size_t elementSize)
        : m_elementSize(elementSize)
    {
        auto inputs = configHelper->GetInputs();
        assert(inputs.size() == 2);
        const auto & features = inputs[configHelper->GetFeatureInputIndex()];
        const auto & labels = inputs[configHelper->GetLabelInputIndex()];

        m_imgChannels = static_cast<int>(features->sampleLayout->GetNumChannels());

        m_inputs.push_back(features);
        m_inputs.push_back(labels);

        size_t labelDimension = labels->sampleLayout->GetHeight();

        m_floatLabelData.resize(labelDimension);
        m_doubleLabelData.resize(labelDimension);

        CreateSequenceDescriptions(configHelper, labelDimension);
    }

    void ImageDataDeserializer::CreateSequenceDescriptions(ImageConfigHelperPtr configHelper, size_t labelDimension)
    {
        UNREFERENCED_PARAMETER(labelDimension);

        std::string mapPath = configHelper->GetMapPath();
        std::ifstream mapFile(mapPath);
        if (!mapFile)
        {
            RuntimeError("Could not open %s for reading.", mapPath.c_str());
        }

        std::string line{ "" };

        ImageSequenceDescription description;
        description.numberOfSamples = 1;
        description.isValid = true;
        for (size_t cline = 0; std::getline(mapFile, line); cline++)
        {
            std::stringstream ss{ line };
            std::string imgPath;
            std::string clsId;
            if (!std::getline(ss, imgPath, '\t') || !std::getline(ss, clsId, '\t'))
            {
                RuntimeError("Invalid map file format, must contain 2 tab-delimited columns: %s, line: %d.", mapPath.c_str(), cline);
            }

            description.id = cline;
            description.chunkId = cline;
            description.path = imgPath;
            description.classId = std::stoi(clsId);
            assert(description.classId < labelDimension);
            m_imageSequences.push_back(description);
        }

        for (const auto& sequence : m_imageSequences)
        {
            m_sequences.push_back(&sequence);
        }
    }

    std::vector<InputDescriptionPtr> ImageDataDeserializer::GetInputs() const
    {
        return m_inputs;
    }

    void ImageDataDeserializer::SetEpochConfiguration(const EpochConfiguration& /* config */)
    {
    }

    const TimelineP& ImageDataDeserializer::GetSequenceDescriptions() const
    {
        return m_sequences;
    }

    std::vector<Sequence> ImageDataDeserializer::GetSequenceById(size_t id)
    {
        assert(id < m_imageSequences.size());
        const auto & imageSequence = m_imageSequences[id];

        // Construct image
        Sequence image;

        m_currentImage = cv::imread(imageSequence.path, cv::IMREAD_COLOR);
        assert(m_currentImage.isContinuous());

        int dataType = m_elementSize == 4 ? CV_32F : CV_64F;

        // Convert element type.
        // TODO this shouldnt be here...
        // eldak Where should this be then ?:)
        if (m_currentImage.type() != CV_MAKETYPE(dataType, m_imgChannels))
        {
            m_currentImage.convertTo(m_currentImage, dataType);
        }

        image.data = m_currentImage.ptr();

        auto imageSampleLayout = std::make_shared<SampleLayout>();
        imageSampleLayout->elementType = m_elementSize == 4 ? et_float : et_double;
        imageSampleLayout->storageType = st_dense;
        imageSampleLayout->dimensions = std::make_shared<ImageLayout>(
            ImageLayoutWHC(m_currentImage.cols, m_currentImage.rows, m_imgChannels));
        image.layout = imageSampleLayout;
        image.numberOfSamples = imageSequence.numberOfSamples;

        // Construct label
        auto labelSampleLayout = std::make_shared<SampleLayout>();
        labelSampleLayout->elementType = m_elementSize == 4 ? et_float : et_double;
        labelSampleLayout->storageType = st_dense;
        labelSampleLayout->dimensions = m_inputs[1]->sampleLayout;

        Sequence label;
        if (m_elementSize == sizeof(float))
        {
            std::fill(m_floatLabelData.begin(), m_floatLabelData.end(), static_cast<float>(0));
            m_floatLabelData[imageSequence.classId] = 1;
            label.data = &m_floatLabelData[0];
        }
        else
        {
            std::fill(m_doubleLabelData.begin(), m_doubleLabelData.end(), 0);
            m_doubleLabelData[imageSequence.classId] = 1;
            label.data = &m_doubleLabelData[0];
        }

        label.layout = labelSampleLayout;
        label.numberOfSamples = imageSequence.numberOfSamples;
        return std::vector<Sequence> { image, label };
    }

    bool ImageDataDeserializer::RequireChunk(size_t /* chunkIndex */)
    {
        return true;
    }

    void ImageDataDeserializer::ReleaseChunk(size_t /* chunkIndex */)
    {
    }
}}}
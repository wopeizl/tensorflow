/* Copyright 2015 The TensorFlow Authors. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// A minimal but useful C++ example showing how to load an Imagenet-style object
// recognition TensorFlow model, prepare input images for it, run them through
// the graph, and interpret the results.
//
// It's designed to have as few dependencies and be as clear as possible, so
// it's more verbose than it could be in production code. In particular, using
// auto for the types of a lot of the returned values from TensorFlow calls can
// remove a lot of boilerplate, but I find the explicit types useful in sample
// code to make it simple to look up the classes involved.
//
// To use it, compile and then run in a working directory with the
// learning/brain/tutorials/label_image/data/ folder below it, and you should
// see the top five labels for the example Lena image output. You can then
// customize it to use your own models or images by changing the file names at
// the top of the main() function.
//
// The googlenet_graph.pb file included by default is created from Inception.
//
// Note that, for GIF inputs, to reuse existing code, only single-frame ones
// are supported.

#include <fstream>
#include <utility>
#include <vector>

#include "tensorflow/cc/ops/const_op.h"
#include "tensorflow/cc/ops/image_ops.h"
#include "tensorflow/cc/ops/standard_ops.h"
#include "tensorflow/core/framework/graph.pb.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/kernels/padding_fifo_queue.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/graph/default_device.h"
#include "tensorflow/core/graph/graph_def_builder.h"
#include "tensorflow/core/lib/core/errors.h"
#include "tensorflow/core/lib/core/stringpiece.h"
#include "tensorflow/core/lib/core/threadpool.h"
#include "tensorflow/core/lib/io/path.h"
#include "tensorflow/core/lib/strings/stringprintf.h"
#include "tensorflow/core/platform/init_main.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/types.h"
#include "tensorflow/core/public/session.h"
#include "tensorflow/core/util/command_line_flags.h"

#include "cv_process.hpp" 

// These are all common classes it's handy to reference with no namespace.
using tensorflow::Flag;
using tensorflow::Tensor;
using tensorflow::Status;
using tensorflow::string;
using tensorflow::int32;
using namespace tensorflow;

// Takes a file name, and loads a list of labels from it, one per line, and
// returns a vector of the strings. It pads with empty strings so the length
// of the result is a multiple of 16, because our model expects that.
Status ReadLabelsFile(const string& file_name, std::vector<string>* result,
    size_t* found_label_count) {
    std::ifstream file(file_name);
    if (!file) {
        return tensorflow::errors::NotFound("Labels file ", file_name,
            " not found.");
    }
    result->clear();
    string line;
    while (std::getline(file, line)) {
        result->push_back(line);
    }
    *found_label_count = result->size();
    const int padding = 16;
    while (result->size() % padding) {
        result->emplace_back();
    }
    return Status::OK();
}

Status ReadTensorFromImageFile3(const string& file_name, const int input_height,
    const int input_width, const float input_mean,
    const float input_std,
    std::vector<Tensor>* out_tensors) {
    const int wanted_channels = 3;

    tensorflow::Tensor image_tensor(
        tensorflow::DT_FLOAT, tensorflow::TensorShape(
    { 1, input_height, input_width, wanted_channels }));
    auto image_tensor_mapped = image_tensor.tensor<float, 4>();
    float *out = image_tensor_mapped.data();
    std::ifstream file(file_name, std::ios::in | std::ios::binary | std::ios::ate);
    std::cout << "read file" << std::endl;
    if (file.is_open())
    {
        int size = file.tellg();
        file.seekg(0, std::ios::beg);
        file.read((char*)out, size);
        std::cout << "finish file" << std::endl;
        file.close();
    }
    out_tensors->push_back(image_tensor);
    return Status::OK();
}

// Given an image file name, read in the data, try to decode it as an image,
// resize it to the requested size, and then scale the values as desired.
Status ReadTensorFromImageFile1(const string& file_name, const int input_height,
    const int input_width, const float input_mean,
    const float input_std,
    std::vector<Tensor>* out_tensors) {
    auto root = tensorflow::Scope::NewRootScope();
    using namespace ::tensorflow::ops;  // NOLINT(build/namespaces)

    string input_name = "file_reader";
    string output_name = "normalized";
    auto file_reader =
        tensorflow::ops::ReadFile(root.WithOpName(input_name), file_name);
    // Now try to figure out what kind of file it is and decode it.
    const int wanted_channels = 3;
    tensorflow::Output image_reader;
    if (tensorflow::StringPiece(file_name).ends_with(".png")) {
        image_reader = DecodePng(root.WithOpName("png_reader"), file_reader,
            DecodePng::Channels(wanted_channels));
    }
    else if (tensorflow::StringPiece(file_name).ends_with(".gif")) {
        // gif decoder returns 4-D tensor, remove the first dim
        image_reader = Squeeze(root.WithOpName("squeeze_first_dim"),
            DecodeGif(root.WithOpName("gif_reader"),
                file_reader));
    }
    else {
        // Assume if it's neither a PNG nor a GIF then it must be a JPEG.
        image_reader = DecodeJpeg(root.WithOpName("jpeg_reader"), file_reader,
            DecodeJpeg::Channels(wanted_channels));
    }
    // Now cast the image data to float so we can do normal math on it.
    auto float_caster =
        Cast(root.WithOpName("float_caster"), image_reader, tensorflow::DT_FLOAT);
    // The convention for image ops in TensorFlow is that all images are expected
    // to be in batches, so that they're four-dimensional arrays with indices of
    // [batch, height, width, channel]. Because we only have a single image, we
    // have to add a batch dimension of 1 to the start with ExpandDims().
    auto dims_expander = ExpandDims(root, float_caster, 0);
    // Bilinearly resize the image to fit the required dimensions.
    auto resized = ResizeBilinear(
        root, dims_expander,
        Const(root.WithOpName("size"), { input_height, input_width }));
    // Subtract the mean and divide by the scale.
    Div(root.WithOpName(output_name), Sub(root, resized, { input_mean }),
        //Div(root.WithOpName(output_name), Sub(root, resized, {103.939, 116.779, 123.68}),
    { input_std });

    // This runs the GraphDef network definition that we've just constructed, and
    // returns the results in the output tensor.
    tensorflow::GraphDef graph;
    TF_RETURN_IF_ERROR(root.ToGraphDef(&graph));

    std::unique_ptr<tensorflow::Session> session(
        tensorflow::NewSession(tensorflow::SessionOptions()));
    TF_RETURN_IF_ERROR(session->Create(graph));
    TF_RETURN_IF_ERROR(session->Run({}, { output_name }, {}, out_tensors));
    return Status::OK();
}

// Reads a model graph definition from disk, and creates a session object you
// can use to run it.
Status LoadGraph(const string& graph_file_name,
    std::unique_ptr<tensorflow::Session>* session, tensorflow::GraphDef& graph_def) {
    //  tensorflow::GraphDef graph_def;
    Status load_graph_status =
        ReadBinaryProto(tensorflow::Env::Default(), graph_file_name, &graph_def);
    if (!load_graph_status.ok()) {
        return tensorflow::errors::NotFound("Failed to load compute graph at '",
            graph_file_name, "'");
    }

    WriteTextProto(tensorflow::Env::Default(), "/tmp/test_inception_v4.pbtxt", graph_def);

    tensorflow::SessionOptions options_;
    options_.config.set_allow_soft_placement(true);

    session->reset(tensorflow::NewSession(options_));
    Status session_create_status = (*session)->Create(graph_def);
    if (!session_create_status.ok()) {
        return session_create_status;
    }
    return Status::OK();
}

Status GetTopLabels(const std::vector<Tensor>& outputs, int how_many_labels,
    Tensor* out_boxes, Tensor* out_indices, Tensor* out_scores) {
    const Tensor& unsorted_scores_tensor = outputs[2];
    auto unsorted_scores_flat = unsorted_scores_tensor.flat<float>();
    std::vector<std::pair<int, float>> scores;
    for (int i = 0; i < unsorted_scores_flat.size(); ++i) {
        scores.push_back(std::pair<int, float>({ i, unsorted_scores_flat(i) }));
    }
    std::sort(scores.begin(), scores.end(),
        [](const std::pair<int, float> &left,
            const std::pair<int, float> &right) {
        return left.second > right.second;
    });
    scores.resize(how_many_labels);
    Tensor sorted_indices(tensorflow::DT_INT32, { (long long)scores.size() });
    Tensor sorted_scores(tensorflow::DT_FLOAT, { (long long)scores.size() });
    for (int i = 0; i < scores.size(); ++i) {
        sorted_indices.flat<int>()(i) = scores[i].first;
        sorted_scores.flat<float>()(i) = scores[i].second;
    }
    *out_indices = sorted_indices;
    *out_scores = sorted_scores;
    return Status::OK();
}

// Analyzes the output of the Inception graph to retrieve the highest scores and
// their positions in the tensor, which correspond to categories.
Status GetTopLabels_topk(const std::vector<Tensor>& outputs, int how_many_labels,
    Tensor* boxes, Tensor* indices, Tensor* scores) {
    auto root = tensorflow::Scope::NewRootScope();
    using namespace ::tensorflow::ops;  // NOLINT(build/namespaces)

    string output_name = "top_k";
    TopK(root.WithOpName(output_name), outputs[2], how_many_labels);
    // This runs the GraphDef network definition that we've just constructed, and
    // returns the results in the output tensors.
    tensorflow::GraphDef graph;
    TF_RETURN_IF_ERROR(root.ToGraphDef(&graph));

    std::unique_ptr<tensorflow::Session> session(
        tensorflow::NewSession(tensorflow::SessionOptions()));
    TF_RETURN_IF_ERROR(session->Create(graph));
    // The TopK node returns two outputs, the scores and their original indices,
    // so we have to append :0 and :1 to specify them both.
    std::vector<Tensor> out_tensors;
    TF_RETURN_IF_ERROR(session->Run({}, { output_name + ":0", output_name + ":1" },
    {}, &out_tensors));
    // *boxes= out_tensors[0];
    *scores = out_tensors[0];
    *indices = out_tensors[1];
    return Status::OK();
}

/*

#include <jpeglib.h>
#include <setjmp.h>

// Error handling for JPEG decoding.
void CatchError(j_common_ptr cinfo) {
(*cinfo->err->output_message)(cinfo);
jmp_buf *jpeg_jmpbuf = reinterpret_cast<jmp_buf *>(cinfo->client_data);
jpeg_destroy(cinfo);
longjmp(*jpeg_jmpbuf, 1);
}


// Decompresses a JPEG file from disk.
Status LoadJpegFile(string file_name, std::vector<tensorflow::uint8>* data,
int* width, int* height, int* channels) {
struct jpeg_decompress_struct cinfo;
FILE * infile;
JSAMPARRAY buffer;
int row_stride;

if ((infile = fopen(file_name.c_str(), "rb")) == NULL) {
LOG(ERROR) << "Can't open " << file_name;
return tensorflow::errors::NotFound("JPEG file ", file_name,
" not found");
}

struct jpeg_error_mgr jerr;
jmp_buf jpeg_jmpbuf;  // recovery point in case of error
cinfo.err = jpeg_std_error(&jerr);
cinfo.client_data = &jpeg_jmpbuf;
jerr.error_exit = CatchError;
if (setjmp(jpeg_jmpbuf)) {
fclose(infile);
return tensorflow::errors::Unknown("JPEG decoding failed");
}

jpeg_create_decompress(&cinfo);
jpeg_stdio_src(&cinfo, infile);
jpeg_read_header(&cinfo, TRUE);
jpeg_start_decompress(&cinfo);
 *width = cinfo.output_width;
 *height = cinfo.output_height;
 *channels = cinfo.output_components;
 data->resize((*height) * (*width) * (*channels));

 row_stride = cinfo.output_width * cinfo.output_components;
 buffer = (*cinfo.mem->alloc_sarray)
 ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
 while (cinfo.output_scanline < cinfo.output_height) {
 tensorflow::uint8* row_address = &((*data)[cinfo.output_scanline * row_stride]);
 jpeg_read_scanlines(&cinfo, buffer, 1);
 memcpy(row_address, buffer[0], row_stride);
 }

 jpeg_finish_decompress(&cinfo);
 jpeg_destroy_decompress(&cinfo);
 fclose(infile);
 return Status::OK();
 }

// Given an image file name, read in the data, try to decode it as an image,
// resize it to the requested size, and then scale the values as desired.
Status ReadTensorFromImageFile(string file_name, const int wanted_height,
const int wanted_width, float input_mean,
const float input_std,
std::vector<Tensor>* out_tensors) {
std::vector<tensorflow::uint8> image_data;
int image_width;
int image_height;
int image_channels;
TF_RETURN_IF_ERROR(LoadJpegFile(file_name, &image_data, &image_width,
&image_height, &image_channels));
LOG(INFO) << "Loaded JPEG: " << image_width << "x" << image_height
<< "x" << image_channels;
const int wanted_channels = 3;
if (image_channels < wanted_channels) {
    return tensorflow::errors::FailedPrecondition("Image needs to have at least ",
            wanted_channels, " but only has ",
            image_channels);
}
// In these loops, we convert the eight-bit data in the image into float, resize
// it using bilinear filtering, and scale it numerically to the float range that
// the model expects (given by input_mean and input_std).
tensorflow::Tensor image_tensor(
        tensorflow::DT_FLOAT, tensorflow::TensorShape(
            {1, wanted_height, wanted_width, wanted_channels}));
auto image_tensor_mapped = image_tensor.tensor<float, 4>();
tensorflow::uint8* in = image_data.data();
float *out = image_tensor_mapped.data();
const size_t image_rowlen = image_width * image_channels;
const float width_scale = static_cast<float>(image_width) / wanted_width;
const float height_scale = static_cast<float>(image_height) / wanted_height;
for (int y = 0; y < wanted_height; ++y) {
    const float in_y = y * height_scale;
    const int top_y_index = static_cast<int>(floorf(in_y));
    const int bottom_y_index =
        std::min(static_cast<int>(ceilf(in_y)), (image_height - 1));
    const float y_lerp = in_y - top_y_index;
    tensorflow::uint8* in_top_row = in + (top_y_index * image_rowlen);
    tensorflow::uint8* in_bottom_row = in + (bottom_y_index * image_rowlen);
    float *out_row = out + (y * wanted_width * wanted_channels);
    for (int x = 0; x < wanted_width; ++x) {
        const float in_x = x * width_scale;
        const int left_x_index = static_cast<int>(floorf(in_x));
        const int right_x_index =
            std::min(static_cast<int>(ceilf(in_x)), (image_width - 1));
        tensorflow::uint8* in_top_left_pixel =
            in_top_row + (left_x_index * wanted_channels);
        tensorflow::uint8* in_top_right_pixel =
            in_top_row + (right_x_index * wanted_channels);
        tensorflow::uint8* in_bottom_left_pixel =
            in_bottom_row + (left_x_index * wanted_channels);
        tensorflow::uint8* in_bottom_right_pixel =
            in_bottom_row + (right_x_index * wanted_channels);
        const float x_lerp = in_x - left_x_index;
        float *out_pixel = out_row + (x * wanted_channels);
        for (int c = 0; c < wanted_channels; ++c) {

            const float top_left((in_top_left_pixel[c] ) / input_std);
            const float top_right((in_top_right_pixel[c] ) / input_std);
            const float bottom_left((in_bottom_left_pixel[c] ) / input_std);
            const float bottom_right((in_bottom_right_pixel[c] ) / input_std);
            const float top = top_left + (top_right - top_left) * x_lerp;
            const float bottom =
                bottom_left + (bottom_right - bottom_left) * x_lerp;
            out_pixel[c] = top + (bottom - top) * y_lerp;

            if(c==0)
                input_mean = 103.939;
            if(c==1)
                input_mean = 116.779;
            if(c==2)
                input_mean = 123.68;


            out_pixel[c] -= input_mean;
        }
    }
}

out_tensors->push_back(image_tensor);
return Status::OK();
}

*/

int main(int argc, char* argv[]) {
    // These are the command-line flags the program can understand.
    // They define where the graph and input data is located, and what kind of
    // input the model expects. If you train your own model, or use something
    // other than inception_v3, then you'll need to update these.
    string image = "tensorflow/examples/label_image/data/grace_hopper.jpg";
    string ckpt = "";
    string graph =
        "tensorflow/examples/label_image/data/inception_v3_2016_08_28_frozen.pb";
    string labels =
        "tensorflow/examples/label_image/data/imagenet_slim_labels.txt";
    int32 input_width = 299;
    int32 input_height = 299;
    int32 input_mean = 0;
    int32 input_std = 255;
    string input_layer = "image_input";
    string output_layer = "InceptionV3/Predictions/Reshape_1";
    bool self_test = false;
    string root_dir = "";
    std::vector<Flag> flag_list = {
        Flag("image", &image, "image to be processed"),
        Flag("graph", &graph, "graph to be executed"),
        Flag("ckpt", &ckpt, "check point"),
        Flag("labels", &labels, "name of file containing labels"),
        Flag("input_width", &input_width, "resize image to this width in pixels"),
        Flag("input_height", &input_height,
                "resize image to this height in pixels"),
        Flag("input_mean", &input_mean, "scale pixel values to this mean"),
        Flag("input_std", &input_std, "scale pixel values to this std deviation"),
        Flag("input_layer", &input_layer, "name of input layer"),
        Flag("output_layer", &output_layer, "name of output layer"),
        Flag("self_test", &self_test, "run a self test"),
        Flag("root_dir", &root_dir,
                "interpret image and graph file names relative to this directory"),
    };
    string usage = tensorflow::Flags::Usage(argv[0], flag_list);
    const bool parse_result = tensorflow::Flags::Parse(&argc, argv, flag_list);
    if (!parse_result) {
        LOG(ERROR) << usage;
        return -1;
    }

    // We need to call this to set up global state for TensorFlow.
    tensorflow::port::InitMain(argv[0], &argc, &argv);
    if (argc > 1) {
        LOG(ERROR) << "Unknown argument " << argv[1] << "\n" << usage;
        return -1;
    }

    // First we load and initialize the model.
    std::unique_ptr<tensorflow::Session> session;
    //tensorflow::Session *session =tensorflow::NewSession(tensorflow::SessionOptions());
    string graph_path = tensorflow::io::JoinPath(root_dir, graph);
    tensorflow::GraphDef graph_def;


    Status load_graph_status = LoadGraph(graph_path, &session, graph_def);
    if (!load_graph_status.ok()) {
        LOG(ERROR) << load_graph_status;
        return -1;
    }
// Get the image from disk as a float array of numbers, resized and normalized
// to the specifications the main graph expects.
    std::vector<Tensor> resized_tensors;
    string image_path = tensorflow::io::JoinPath(root_dir, image);
    /*
       Status read_tensor_status =
       ReadTensorFromImageFile(image_path, input_height, input_width, input_mean,
       input_std, &resized_tensors);
       if (!read_tensor_status.ok()) {
       LOG(ERROR) << read_tensor_status;
       return -1;
       }
       */

    cv::Mat mat = cv::imread(image_path, CV_LOAD_IMAGE_COLOR);
    std::vector<unsigned char> odata;
    cv::Mat img;
    cvprocess::resizeImage(mat, 103.939, 116.779, 123.68, input_width, input_height, img);
    cv::imwrite("./img.png", img);

    //cvprocess::resizeImage(mat, 123.68, 116.779, 103.939, input_width, input_height, img);

    Tensor inputImg(tensorflow::DT_FLOAT, { 1,input_height,input_width,3 });
    auto inputImageMapped = inputImg.tensor<float, 4>();
    //Copy all the data over
    for (int y = 0; y < input_height; ++y) {
        const float* source_row = ((float*)img.data) + (y * input_width * 3);
        for (int x = 0; x < input_width; ++x) {
            const float* source_pixel = source_row + (x * 3);
            inputImageMapped(0, y, x, 0) = source_pixel[0];
            inputImageMapped(0, y, x, 1) = source_pixel[1];
            inputImageMapped(0, y, x, 2) = source_pixel[2];
        }
    }

    resized_tensors.push_back(inputImg);

    const Tensor& resized_tensor = resized_tensors[0];

    //std::cout << "==========================================" << std::endl;
    //std::cout << resized_tensor.dims() << std::endl;

    // Actually run the image through the model.
    std::vector<Tensor> outputs;
    std::vector<string> olabels = { "bbox/trimming/bbox","probability/class_idx","probability/score" };

    tensorflow::TensorShape image_input_shape;
    image_input_shape.AddDim(1);
    image_input_shape.AddDim(384);
    image_input_shape.AddDim(624);
    image_input_shape.AddDim(3);

    tensorflow::TensorShape box_mask_shape;
    box_mask_shape.AddDim(1);
    box_mask_shape.AddDim(16848);
    box_mask_shape.AddDim(1);
    Tensor box_mask_tensor(tensorflow::DataType::DT_FLOAT, box_mask_shape);

    tensorflow::TensorShape box_delta_input_shape;
    box_delta_input_shape.AddDim(1);
    box_delta_input_shape.AddDim(16848);
    box_delta_input_shape.AddDim(4);
    Tensor box_delta_input_tensor(tensorflow::DataType::DT_FLOAT, box_delta_input_shape);

    tensorflow::TensorShape box_input_shape;
    box_input_shape.AddDim(1);
    box_input_shape.AddDim(16848);
    box_input_shape.AddDim(4);
    Tensor box_input_tensor(tensorflow::DataType::DT_FLOAT, box_input_shape);

    tensorflow::TensorShape labels_shape;
    labels_shape.AddDim(1);
    labels_shape.AddDim(16848);
    labels_shape.AddDim(3);
    Tensor labels_tensor(tensorflow::DataType::DT_FLOAT, labels_shape);

    std::cout << "-------------step 1 -------------" << std::endl;
    Status run_status = session->Run({ {"image_input", resized_tensor}, {"box_mask", box_mask_tensor}, {"box_delta_input", box_delta_input_tensor}, {"box_input", box_input_tensor}, {"labels", labels_tensor} },
        {}, { "fifo_queue_EnqueueMany" }, nullptr);

    // Status run_status = session->Run({{input_layer, resized_tensor}},{},{"fifo_queue_EnqueueMany"}, nullptr);
    if (!run_status.ok()) {
        LOG(ERROR) << "Running model failed: " << run_status;
        return -1;
    }
    else {
        LOG(ERROR) << "Running model success: " << run_status;
    }

    std::cout << "-------------step 2 -------------" << std::endl;
    run_status = session->Run({},
    {}, { "batch/fifo_queue_enqueue" }, nullptr);

    // Status run_status = session->Run({{input_layer, resized_tensor}},{},{"fifo_queue_EnqueueMany"}, nullptr);
    if (!run_status.ok()) {
        LOG(ERROR) << "Running model failed: " << run_status;
        return -1;
    }
    else {
        LOG(ERROR) << "Running model success: " << run_status;
    }

    std::cout << "------------step two--------------" << std::endl;
    run_status = session->Run({},
        olabels, {}, &outputs);

    if (!run_status.ok()) {
        LOG(ERROR) << "Running model failed: " << run_status;
        return -1;
    }
    else {
        LOG(ERROR) << "Running model success: " << run_status;

    }

    Tensor boxes = outputs[0];
    Tensor oindices = outputs[1];
    Tensor scores = outputs[2];

    Tensor oboxes;
    Tensor ooindices;
    Tensor oscores;

    //std::cout << boxes.dim_size(0) << std::endl;
    //std::cout << oindices.dim_size(0) << std::endl;
    //std::cout << scores.dim_size(0) << std::endl;

    //std::cout << boxes.DebugString() << std::endl;
    //std::cout << oindices.DebugString() << std::endl;
    //std::cout << scores.DebugString() << std::endl;

    //std::cout << boxes.SummarizeValue(100) << std::endl;
    //std::cout << oindices.SummarizeValue(100) << std::endl;
    //std::cout << scores.SummarizeValue(100) << std::endl;

    //GetTopLabels(outputs, 3, &oboxes, &ooindices, &oscores);
    //tensorflow::TTypes<float>::Flat oscores_flat = oscores.flat<float>();
    //tensorflow::TTypes<int>::Flat ooindices_flat = ooindices.flat<int>();
    //for (int pos = 0; pos < 3; ++pos) {
    //    int label_index = ooindices_flat(pos);
    //    const float oscore = oscores_flat(pos);

    //    typename TTypes<float, 3>::Tensor obox = boxes.tensor<float, 3>();
    //    int cx = obox(0, label_index, 0);
    //    int cy = obox(0, label_index, 1);
    //    int w = obox(0, label_index, 2);
    //    int h = obox(0, label_index, 3);
    //    int x1 = cx - w / 2;
    //    int y1 = cy - h / 2;
    //    int x2 = cx + w / 2;
    //    int y2 = cy + h / 2;

    //    cv::rectangle(img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 0, 255), 2);
    //    char text[64];
    //    sprintf(text, "%lf", oscore);
    //    cv::putText(img, text
    //        , cv::Point(x1, y1)
    //        , CV_FONT_HERSHEY_COMPLEX
    //        , 0.8
    //        , cv::Scalar(0, 0, 255));

    //}
    //cv::imwrite("./test.png", img);


    //return 0;

    float scale_factor_w = (float)img.cols / mat.cols;
    float scale_factor_h = (float)img.rows / mat.rows;

    GetTopLabels(outputs, 3, &boxes, &ooindices, &scores);
    //std::cout << "=================111=========================" << std::endl;
    tensorflow::TTypes<float>::Flat scores_flat = scores.flat<float>();
    //std::cout << "=================222=========================" << std::endl;
    tensorflow::TTypes<long long>::Flat indices_flat = oindices.flat<long long>();
    tensorflow::TTypes<int>::Flat oindices_flat = ooindices.flat<int>();
    //std::cout << "=================333=========================" << std::endl;
    for (int pos = 0; pos < 3; ++pos) {
        //std::cout << "=================444=========================" << std::endl;
        int label_index = oindices_flat(pos);
        const float score = scores_flat(pos);

        LOG(INFO) << indices_flat(label_index) << " (" << label_index << "): " << score;

        typename TTypes<float, 3>::Tensor obox = boxes.tensor<float, 3>();
            int cx = obox(0, label_index, 0);
            int cy = obox(0, label_index, 1);
            int w = obox(0, label_index, 2);
            int h = obox(0, label_index, 3);
            int x1 = cx - w / 2;
            int y1 = cy - h / 2;
            int x2 = cx + w / 2;
            int y2 = cy + h / 2;
            x1 /= scale_factor_w;
            x2 /= scale_factor_w;
            y1 /= scale_factor_h;
            y2 /= scale_factor_h;

        cv::rectangle(mat, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0), 2);
        char text[64];
        sprintf(text, "%lf", score);
        cv::putText(mat, text
            , cv::Point(x1, y1)
            , CV_FONT_HERSHEY_COMPLEX
            , 0.8
            , cv::Scalar(0, 0, 255));
    }
    cv::imwrite("./test.png", mat);

    return 0;
}

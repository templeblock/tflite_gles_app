/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2020 terryky1220@gmail.com
 * ------------------------------------------------ */
#include "util_tflite.h"
#include "tflite_facemesh.h"
#include <list>

/* 
 * https://github.com/google/mediapipe/tree/master/mediapipe/models/face_detection_front.tflite
 * https://github.com/google/mediapipe/blob/master/mediapipe/models/face_landmark.tflite
 * https://github.com/google/mediapipe/blob/master/mediapipe/models/iris_landmark.tflite
 */
#define FACE_DETECTL_MODEL_PATH    "./facemesh_model/face_detection_front.tflite"
#define FACE_LANDMARK_MODEL_PATH   "./facemesh_model/face_landmark.tflite"
#define IRIS_LANDMARK_MODEL_PATH   "./models/iris_landmark.tflite"

#define FACE_DETECTL_QUANT_MODEL_PATH    "./facemesh_model/face_detection_front_128_full_integer_quant.tflite"
#define FACE_LANDMARK_QUANT_MODEL_PATH   "./facemesh_model/face_landmark_192_full_integer_quant.tflite"

static tflite_interpreter_t s_detect_interpreter;
static tflite_tensor_t      s_detect_tensor_input;
static tflite_tensor_t      s_detect_tensor_scores;
static tflite_tensor_t      s_detect_tensor_bboxes;

static tflite_interpreter_t s_mesh_interpreter;
static tflite_tensor_t      s_mesh_tensor_input;
static tflite_tensor_t      s_mesh_tensor_landmark;
static tflite_tensor_t      s_mesh_tensor_score;

static tflite_interpreter_t s_iris_interpreter;
static tflite_tensor_t      s_iris_tensor_input;
static tflite_tensor_t      s_iris_tensor_iris;
static tflite_tensor_t      s_iris_tensor_eye;

static std::list<fvec2> s_anchors;

/*
 * determine where the anchor points are scatterd.
 *   https://github.com/tensorflow/tfjs-models/blob/master/blazeface/src/face.ts
 */
static int
create_blazeface_anchors(int input_w, int input_h)
{
    /* ANCHORS_CONFIG  */
    int strides[2] = {8, 16};
    int anchors[2] = {2,  6};

    int numtotal = 0;

    for (int i = 0; i < 2; i ++)
    {
        int stride = strides[i];
        int gridCols = (input_w + stride -1) / stride;
        int gridRows = (input_h + stride -1) / stride;
        int anchorNum = anchors[i];

        fvec2 anchor;
        for (int gridY = 0; gridY < gridRows; gridY ++)
        {
            anchor.y = stride * (gridY + 0.5f);
            for (int gridX = 0; gridX < gridCols; gridX ++)
            {
                anchor.x = stride * (gridX + 0.5f);
                for (int n = 0; n < anchorNum; n ++)
                {
                    s_anchors.push_back (anchor);
                    numtotal ++;
                }
            }
        }
    }
    return numtotal;
}



/* -------------------------------------------------- *
 *  Create TFLite Interpreter
 * -------------------------------------------------- */
int
init_tflite_facemesh (int use_quantized_tflite)
{
    const char *detect_model;
    const char *mesh_model;
    const char *iris_model;

    if (use_quantized_tflite)
    {
        detect_model = FACE_DETECTL_QUANT_MODEL_PATH;
        mesh_model   = FACE_LANDMARK_QUANT_MODEL_PATH;
        iris_model   = IRIS_LANDMARK_MODEL_PATH;
    }
    else
    {
        detect_model = FACE_DETECTL_MODEL_PATH;
        mesh_model   = FACE_LANDMARK_MODEL_PATH;
        iris_model   = IRIS_LANDMARK_MODEL_PATH;
    }

    /* Face detect */
    tflite_create_interpreter_from_file (&s_detect_interpreter, detect_model);
    tflite_get_tensor_by_name (&s_detect_interpreter, 0, "input",          &s_detect_tensor_input);
    tflite_get_tensor_by_name (&s_detect_interpreter, 1, "regressors",     &s_detect_tensor_bboxes);
    tflite_get_tensor_by_name (&s_detect_interpreter, 1, "classificators", &s_detect_tensor_scores);

    /* Facemesh Landmark */
    tflite_create_interpreter_from_file (&s_mesh_interpreter, mesh_model);
    tflite_get_tensor_by_name (&s_mesh_interpreter, 0, "input_1",   &s_mesh_tensor_input);
    tflite_get_tensor_by_name (&s_mesh_interpreter, 1, "conv2d_20", &s_mesh_tensor_landmark);
    tflite_get_tensor_by_name (&s_mesh_interpreter, 1, "conv2d_30", &s_mesh_tensor_score);

    /* Iris Landmark */
    tflite_create_interpreter_from_file (&s_iris_interpreter, iris_model);
    tflite_get_tensor_by_name (&s_iris_interpreter, 0, "input_1",                        &s_iris_tensor_input);
    tflite_get_tensor_by_name (&s_iris_interpreter, 1, "output_eyes_contours_and_brows", &s_iris_tensor_eye);
    tflite_get_tensor_by_name (&s_iris_interpreter, 1, "output_iris",                    &s_iris_tensor_iris);

    int det_input_w = s_detect_tensor_input.dims[2];
    int det_input_h = s_detect_tensor_input.dims[1];
    create_blazeface_anchors (det_input_w, det_input_h);

    return 0;
}

void *
get_face_detect_input_buf (int *w, int *h)
{
    *w = s_detect_tensor_input.dims[2];
    *h = s_detect_tensor_input.dims[1];
    return s_detect_tensor_input.ptr;
}

void *
get_facemesh_landmark_input_buf (int *w, int *h)
{
    *w = s_mesh_tensor_input.dims[2];
    *h = s_mesh_tensor_input.dims[1];
    return s_mesh_tensor_input.ptr;
}

void *
get_irismesh_landmark_input_buf (int *w, int *h)
{
    *w = s_iris_tensor_input.dims[2];
    *h = s_iris_tensor_input.dims[1];
    return s_iris_tensor_input.ptr;
}

int
capture_to_img (char *lpFName, int nW, int nH, float *lpBuf)
{
    FILE *fp;
    char strFName[ 128 ];
    static int s_ncnt = 0;
    unsigned char *lpbuf8;
    
    sprintf (strFName, "%s_%05d_RGB888_SIZE%dx%d.img", lpFName, s_ncnt, nW, nH);
    s_ncnt ++;

    fp = fopen (strFName, "wb");
    if (fp == NULL)
    {
        fprintf (stderr, "FATAL ERROR at %s(%d)\n", __FILE__, __LINE__);
        return -1;
    }

    lpbuf8 = (unsigned char *)malloc (nW * nH * 3);
    for (int y = 0; y < nH * nW; y ++)
    {
        lpbuf8[y * 3 + 0] = lpBuf[y * 3 + 0] * 255;
        lpbuf8[y * 3 + 1] = lpBuf[y * 3 + 1] * 255;
        lpbuf8[y * 3 + 2] = lpBuf[y * 3 + 2] * 255;
    }
    
    fwrite (lpbuf8, 3, nW * nH, fp);

    free (lpbuf8);
    fclose (fp);
    
    return 0;
}

/* -------------------------------------------------- *
 * Invoke TensorFlow Lite (Face detection)
 * -------------------------------------------------- */
static float *
get_bbox_ptr (int anchor_idx)
{
    int idx = 16 * anchor_idx;
    float *bboxes_ptr = (float *)s_detect_tensor_bboxes.ptr;

    return &bboxes_ptr[idx];
}

static int
decode_bounds (std::list<face_t> &face_list, float score_thresh, int input_img_w, int input_img_h)
{
    face_t face_item;
    float  *scores_ptr = (float *)s_detect_tensor_scores.ptr;
    
    int i = 0;
    for (auto itr = s_anchors.begin(); itr != s_anchors.end(); i ++, itr ++)
    {
        fvec2 anchor = *itr;
        float score0 = scores_ptr[i];
        float score = 1.0f / (1.0f + exp(-score0));

        if (score > score_thresh)
        {
            float *p = get_bbox_ptr (i);

            /* boundary box */
            float sx = p[0];
            float sy = p[1];
            float w  = p[2];
            float h  = p[3];

            float cx = sx + anchor.x;
            float cy = sy + anchor.y;

            cx /= (float)input_img_w;
            cy /= (float)input_img_h;
            w  /= (float)input_img_w;
            h  /= (float)input_img_h;

            fvec2 topleft, btmright;
            topleft.x  = cx - w * 0.5f;
            topleft.y  = cy - h * 0.5f;
            btmright.x = cx + w * 0.5f;
            btmright.y = cy + h * 0.5f;

            face_item.score    = score;
            face_item.topleft  = topleft;
            face_item.btmright = btmright;

            /* landmark positions (6 keys) */
            for (int j = 0; j < kFaceKeyNum; j ++)
            {
                float lx = p[4 + (2 * j) + 0];
                float ly = p[4 + (2 * j) + 1];
                lx += anchor.x;
                ly += anchor.y;
                lx /= (float)input_img_w;
                ly /= (float)input_img_h;

                face_item.keys[j].x = lx;
                face_item.keys[j].y = ly;
            }

            face_list.push_back (face_item);
        }
    }
    return 0;
}

/* -------------------------------------------------- *
 *  Apply NonMaxSuppression:
 *      https://github.com/tensorflow/tfjs/blob/master/tfjs-core/src/ops/image_ops.ts
 * -------------------------------------------------- */
static float
calc_intersection_over_union (face_t &face0, face_t &face1)
{
    float sx0 = face0.topleft.x;
    float sy0 = face0.topleft.y;
    float ex0 = face0.btmright.x;
    float ey0 = face0.btmright.y;
    float sx1 = face1.topleft.x;
    float sy1 = face1.topleft.y;
    float ex1 = face1.btmright.x;
    float ey1 = face1.btmright.y;
    
    float xmin0 = std::min (sx0, ex0);
    float ymin0 = std::min (sy0, ey0);
    float xmax0 = std::max (sx0, ex0);
    float ymax0 = std::max (sy0, ey0);
    float xmin1 = std::min (sx1, ex1);
    float ymin1 = std::min (sy1, ey1);
    float xmax1 = std::max (sx1, ex1);
    float ymax1 = std::max (sy1, ey1);
    
    float area0 = (ymax0 - ymin0) * (xmax0 - xmin0);
    float area1 = (ymax1 - ymin1) * (xmax1 - xmin1);
    if (area0 <= 0 || area1 <= 0)
        return 0.0f;

    float intersect_xmin = std::max (xmin0, xmin1);
    float intersect_ymin = std::max (ymin0, ymin1);
    float intersect_xmax = std::min (xmax0, xmax1);
    float intersect_ymax = std::min (ymax0, ymax1);

    float intersect_area = std::max (intersect_ymax - intersect_ymin, 0.0f) *
                           std::max (intersect_xmax - intersect_xmin, 0.0f);
    
    return intersect_area / (area0 + area1 - intersect_area);
}

static bool
compare (face_t &v1, face_t &v2)
{
    if (v1.score > v2.score)
        return true;
    else
        return false;
}

static int
non_max_suppression (std::list<face_t> &face_list, std::list<face_t> &face_sel_list, float iou_thresh)
{
    face_list.sort (compare);

    for (auto itr = face_list.begin(); itr != face_list.end(); itr ++)
    {
        face_t face_candidate = *itr;

        int ignore_candidate = false;
        for (auto itr_sel = face_sel_list.rbegin(); itr_sel != face_sel_list.rend(); itr_sel ++)
        {
            face_t face_sel = *itr_sel;

            float iou = calc_intersection_over_union (face_candidate, face_sel);
            if (iou >= iou_thresh)
            {
                ignore_candidate = true;
                break;
            }
        }

        if (!ignore_candidate)
        {
            face_sel_list.push_back(face_candidate);
            if (face_sel_list.size() >= MAX_FACE_NUM)
                break;
        }
    }

    return 0;
}

/* -------------------------------------------------- *
 *  Scale bbox
 * -------------------------------------------------- */
static float
normalize_radians (float angle)
{
    return angle - 2 * M_PI * std::floor((angle - (-M_PI)) / (2 * M_PI));
}

static void
compute_rotation (face_t &face)
{
    float x0 = face.keys[kRightEye].x;
    float y0 = face.keys[kRightEye].y;
    float x1 = face.keys[kLeftEye].x;
    float y1 = face.keys[kLeftEye].y;

    float target_angle = 0;//M_PI * 0.5f;
    float rotation = target_angle - std::atan2(-(y1 - y0), x1 - x0);

    face.rotation = normalize_radians (rotation);
}

static void
rot_vec (fvec2 &vec, float rotation)
{
    float sx = vec.x;
    float sy = vec.y;
    vec.x = sx * std::cos(rotation) - sy * std::sin(rotation);
    vec.y = sx * std::sin(rotation) + sy * std::cos(rotation);
}

static void
compute_face_rect (face_t &face)
{
    float width    = face.btmright.x - face.topleft.x;
    float height   = face.btmright.y - face.topleft.y;
    float palm_cx  = face.topleft.x + width  * 0.5f;
    float palm_cy  = face.topleft.y + height * 0.5f;
    float face_cx;
    float face_cy;
    float rotation = face.rotation;
    float shift_x = 0;// 0.0f;
    float shift_y = 0;//-0.5f;

    if (rotation == 0.0f)
    {
        face_cx = palm_cx + (width  * shift_x);
        face_cy = palm_cy + (height * shift_y);
    }
    else
    {
        float dx = (width  * shift_x) * std::cos(rotation) -
                   (height * shift_y) * std::sin(rotation);
        float dy = (width  * shift_x) * std::sin(rotation) +
                   (height * shift_y) * std::cos(rotation);
        face_cx = palm_cx + dx;
        face_cy = palm_cy + dy;
    }

    float long_side = std::max (width, height);
    width  = long_side;
    height = long_side;
    float face_w = width  * 1.5f;
    float face_h = height * 1.5f;

    face.face_cx = face_cx;
    face.face_cy = face_cy;
    face.face_w  = face_w;
    face.face_h  = face_h;

    float dx = face_w * 0.5f;
    float dy = face_h * 0.5f;

    face.face_pos[0].x = - dx;  face.face_pos[0].y = - dy;
    face.face_pos[1].x = + dx;  face.face_pos[1].y = - dy;
    face.face_pos[2].x = + dx;  face.face_pos[2].y = + dy;
    face.face_pos[3].x = - dx;  face.face_pos[3].y = + dy;

    for (int i = 0; i < 4; i ++)
    {
        rot_vec (face.face_pos[i], rotation);
        face.face_pos[i].x += face_cx;
        face.face_pos[i].y += face_cy;
    }
}


static void
pack_face_result (face_detect_result_t *facedet_result, std::list<face_t> &face_list)
{
    int num_faces = 0;
    for (auto itr = face_list.begin(); itr != face_list.end(); itr ++)
    {
        face_t face = *itr;

        compute_rotation (face);
        compute_face_rect (face);

        memcpy (&facedet_result->faces[num_faces], &face, sizeof (face));
        num_faces ++;
        facedet_result->num = num_faces;

        if (num_faces >= MAX_FACE_NUM)
            break;
    }
}


/* -------------------------------------------------- *
 * Invoke TensorFlow Lite
 * -------------------------------------------------- */
int
invoke_face_detect (face_detect_result_t *facedet_result)
{
    //capture_to_img ("detect", s_detect_tensor_input.dims[2], s_detect_tensor_input.dims[1], (float *)s_detect_tensor_input.ptr);
    if (s_detect_interpreter.interpreter->Invoke() != kTfLiteOk)
    {
        fprintf (stderr, "ERR: %s(%d)\n", __FILE__, __LINE__);
        return -1;
    }

    /* decode boundary box and landmark keypoints */
    float score_thresh = 0.75f;
    std::list<face_t> face_list;

    int input_img_w = s_detect_tensor_input.dims[2];
    int input_img_h = s_detect_tensor_input.dims[1];
    decode_bounds (face_list, score_thresh, input_img_w, input_img_h);


#if 1 /* USE NMS */
    float iou_thresh = 0.3f;
    std::list<face_t> face_nms_list;

    non_max_suppression (face_list, face_nms_list, iou_thresh);
    pack_face_result (facedet_result, face_nms_list);
#else
    pack_face_result (facedet_result, face_list);
#endif

    return 0;
}

 
/* -------------------------------------------------- *
 * Invoke TensorFlow Lite (Facemesh landmark)
 * -------------------------------------------------- */
static void
compute_eye_roi_one (face_landmark_result_t *facemesh_result, int id, int idx0, int idx1)
{
    float x0 = facemesh_result->joint[idx0].x;
    float y0 = facemesh_result->joint[idx0].y;
    float x1 = facemesh_result->joint[idx1].x;
    float y1 = facemesh_result->joint[idx1].y;

    float cx = (x0 + x1) / 2.0f;
    float cy = (y0 + y1) / 2.0f;
    float w = std::abs(x1 - x0);
    float h = std::abs(y1 - y0);

    {
        float long_side = std::max (w, h);
        w = h = long_side;
    }
    {
        float scale = 2.3f;
        w *= scale;
        h *= scale;
    }

    float dx = w / 2.0f;
    float dy = h / 2.0f;

    fvec2 *eye_pos = facemesh_result->eye_pos[id];
    eye_pos[0].x = -dx;  eye_pos[0].y = -dy;
    eye_pos[1].x = +dx;  eye_pos[1].y = -dy;
    eye_pos[2].x = +dx;  eye_pos[2].y = +dy;
    eye_pos[3].x = -dx;  eye_pos[3].y = +dy;

    float rotation = 0.0f;
    for (int i = 0; i < 4; i ++)
    {
        rot_vec (*eye_pos, rotation);
        eye_pos[i].x += cx;
        eye_pos[i].y += cy;
    }

    eye_region_t *eye_rgn = &facemesh_result->eye_rgn[id];
    eye_rgn->rotation = rotation;
    eye_rgn->center.x = cx;
    eye_rgn->center.y = cy;
    eye_rgn->size.x   = w;
    eye_rgn->size.y   = h;
}

static void
compute_eye_roi (face_landmark_result_t *facemesh_result)
{
    compute_eye_roi_one (facemesh_result, 0,  33, 133);
    compute_eye_roi_one (facemesh_result, 1, 362, 263);
}
 
int
invoke_facemesh_landmark (face_landmark_result_t *facemesh_result)
{
    //capture_to_img ("mesh", s_mesh_tensor_input.dims[2], s_mesh_tensor_input.dims[1], (float *)s_mesh_tensor_input.ptr);
    if (s_mesh_interpreter.interpreter->Invoke() != kTfLiteOk)
    {
        fprintf (stderr, "ERR: %s(%d)\n", __FILE__, __LINE__);
        return -1;
    }

    float *meshscore_ptr = (float *)s_mesh_tensor_score.ptr;
    float *landmark_ptr  = (float *)s_mesh_tensor_landmark.ptr;
    int img_w = s_mesh_tensor_input.dims[2];
    int img_h = s_mesh_tensor_input.dims[1];

    facemesh_result->score = *meshscore_ptr;
    //fprintf (stderr, "meshscore = %f\n", *meshscore_ptr);
    
    for (int i = 0; i < FACE_KEY_NUM; i ++)
    {
        facemesh_result->joint[i].x = landmark_ptr[3 * i + 0] / (float)img_w;
        facemesh_result->joint[i].y = landmark_ptr[3 * i + 1] / (float)img_h;
        facemesh_result->joint[i].z = landmark_ptr[3 * i + 2];
        //fprintf (stderr, "[%2d] (%8.1f, %8.1f, %8.1f)\n", i, 
        //    landmark_ptr[3 * i + 0], landmark_ptr[3 * i + 1], landmark_ptr[3 * i + 2]);
    }

    compute_eye_roi (facemesh_result);

    return 0;
}

/* -------------------------------------------------- *
 * Invoke TensorFlow Lite (Irismesh landmark)
 * -------------------------------------------------- */



int
invoke_irismesh_landmark (irismesh_result_t *irismesh_result)
{
    //capture_to_img ("iris", 64, 64, (float *)s_iris_tensor_input.ptr);
    //fprintf (stderr, "DUMP: %p\n", s_iris_tensor_input.ptr);
    
    if (s_iris_interpreter.interpreter->Invoke() != kTfLiteOk)
    {
        fprintf (stderr, "ERR: %s(%d)\n", __FILE__, __LINE__);
        return -1;
    }

    float *eye_landmark_ptr = (float *)s_iris_tensor_eye.ptr;
    float *landmark_ptr     = (float *)s_iris_tensor_iris.ptr;
    int img_w = s_iris_tensor_input.dims[2];
    int img_h = s_iris_tensor_input.dims[1];

    for (int i = 0; i < 71; i ++)
    {
        irismesh_result->eye_landmark[i].x = eye_landmark_ptr[3 * i + 0] / (float)img_w;
        irismesh_result->eye_landmark[i].y = eye_landmark_ptr[3 * i + 1] / (float)img_h;
        irismesh_result->eye_landmark[i].z = eye_landmark_ptr[3 * i + 2];
        // fprintf (stderr, "[%2d] (%8.1f, %8.1f, %8.1f)\n", i, 
        //    eye_landmark_ptr[3 * i + 0], eye_landmark_ptr[3 * i + 1], eye_landmark_ptr[3 * i + 2]);
    }

    for (int i = 0; i < 5; i ++)
    {
        irismesh_result->iris_landmark[i].x = landmark_ptr[3 * i + 0] / (float)img_w;
        irismesh_result->iris_landmark[i].y = landmark_ptr[3 * i + 1] / (float)img_h;
        irismesh_result->iris_landmark[i].z = landmark_ptr[3 * i + 2];
        //fprintf (stderr, "[%2d] (%8.1f, %8.1f, %8.1f)\n", i, 
        //    landmark_ptr[3 * i + 0], landmark_ptr[3 * i + 1], landmark_ptr[3 * i + 2]);
    }

    return 0;
}



/*
 * Mesh Indices.
 * https://github.com/tensorflow/tfjs-models/blob/master/facemesh/demo/triangulation.js
 */
/**
 * @license
 * Copyright 2020 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * =============================================================================
 */
static int s_face_tris[] = 
{
    127, 34, 139, 11, 0, 37, 232, 231, 120, 72, 37, 39, 128, 121, 47, 232, 121,
    128, 104, 69, 67, 175, 171, 148, 157, 154, 155, 118, 50, 101, 73, 39, 40, 9,
    151, 108, 48, 115, 131, 194, 204, 211, 74, 40, 185, 80, 42, 183, 40, 92,
    186, 230, 229, 118, 202, 212, 214, 83, 18, 17, 76, 61, 146, 160, 29, 30, 56,
    157, 173, 106, 204, 194, 135, 214, 192, 203, 165, 98, 21, 71, 68, 51, 45, 4,
    144, 24, 23, 77, 146, 91, 205, 50, 187, 201, 200, 18, 91, 106, 182, 90, 91,
    181, 85, 84, 17, 206, 203, 36, 148, 171, 140, 92, 40, 39, 193, 189, 244,
    159, 158, 28, 247, 246, 161, 236, 3, 196, 54, 68, 104, 193, 168, 8, 117,
    228, 31, 189, 193, 55, 98, 97, 99, 126, 47, 100, 166, 79, 218, 155, 154, 26,
    209, 49, 131, 135, 136, 150, 47, 126, 217, 223, 52, 53, 45, 51, 134, 211,
    170, 140, 67, 69, 108, 43, 106, 91, 230, 119, 120, 226, 130, 247, 63, 53,
    52, 238, 20, 242, 46, 70, 156, 78, 62, 96, 46, 53, 63, 143, 34, 227, 173,
    155, 133, 123, 117, 111, 44, 125, 19, 236, 134, 51, 216, 206, 205, 154, 153,
    22, 39, 37, 167, 200, 201, 208, 36, 142, 100, 57, 212, 202, 20, 60, 99, 28,
    158, 157, 35, 226, 113, 160, 159, 27, 204, 202, 210, 113, 225, 46, 43, 202,
    204, 62, 76, 77, 137, 123, 116, 41, 38, 72, 203, 129, 142, 64, 98, 240, 49,
    102, 64, 41, 73, 74, 212, 216, 207, 42, 74, 184, 169, 170, 211, 170, 149,
    176, 105, 66, 69, 122, 6, 168, 123, 147, 187, 96, 77, 90, 65, 55, 107, 89,
    90, 180, 101, 100, 120, 63, 105, 104, 93, 137, 227, 15, 86, 85, 129, 102,
    49, 14, 87, 86, 55, 8, 9, 100, 47, 121, 145, 23, 22, 88, 89, 179, 6, 122,
    196, 88, 95, 96, 138, 172, 136, 215, 58, 172, 115, 48, 219, 42, 80, 81, 195,
    3, 51, 43, 146, 61, 171, 175, 199, 81, 82, 38, 53, 46, 225, 144, 163, 110,
    246, 33, 7, 52, 65, 66, 229, 228, 117, 34, 127, 234, 107, 108, 69, 109, 108,
    151, 48, 64, 235, 62, 78, 191, 129, 209, 126, 111, 35, 143, 163, 161, 246,
    117, 123, 50, 222, 65, 52, 19, 125, 141, 221, 55, 65, 3, 195, 197, 25, 7,
    33, 220, 237, 44, 70, 71, 139, 122, 193, 245, 247, 130, 33, 71, 21, 162,
    153, 158, 159, 170, 169, 150, 188, 174, 196, 216, 186, 92, 144, 160, 161, 2,
    97, 167, 141, 125, 241, 164, 167, 37, 72, 38, 12, 145, 159, 160, 38, 82, 13,
    63, 68, 71, 226, 35, 111, 158, 153, 154, 101, 50, 205, 206, 92, 165, 209,
    198, 217, 165, 167, 97, 220, 115, 218, 133, 112, 243, 239, 238, 241, 214,
    135, 169, 190, 173, 133, 171, 208, 32, 125, 44, 237, 86, 87, 178, 85, 86,
    179, 84, 85, 180, 83, 84, 181, 201, 83, 182, 137, 93, 132, 76, 62, 183, 61,
    76, 184, 57, 61, 185, 212, 57, 186, 214, 207, 187, 34, 143, 156, 79, 239,
    237, 123, 137, 177, 44, 1, 4, 201, 194, 32, 64, 102, 129, 213, 215, 138, 59,
    166, 219, 242, 99, 97, 2, 94, 141, 75, 59, 235, 24, 110, 228, 25, 130, 226,
    23, 24, 229, 22, 23, 230, 26, 22, 231, 112, 26, 232, 189, 190, 243, 221, 56,
    190, 28, 56, 221, 27, 28, 222, 29, 27, 223, 30, 29, 224, 247, 30, 225, 238,
    79, 20, 166, 59, 75, 60, 75, 240, 147, 177, 215, 20, 79, 166, 187, 147, 213,
    112, 233, 244, 233, 128, 245, 128, 114, 188, 114, 217, 174, 131, 115, 220,
    217, 198, 236, 198, 131, 134, 177, 132, 58, 143, 35, 124, 110, 163, 7, 228,
    110, 25, 356, 389, 368, 11, 302, 267, 452, 350, 349, 302, 303, 269, 357,
    343, 277, 452, 453, 357, 333, 332, 297, 175, 152, 377, 384, 398, 382, 347,
    348, 330, 303, 304, 270, 9, 336, 337, 278, 279, 360, 418, 262, 431, 304,
    408, 409, 310, 415, 407, 270, 409, 410, 450, 348, 347, 422, 430, 434, 313,
    314, 17, 306, 307, 375, 387, 388, 260, 286, 414, 398, 335, 406, 418, 364,
    367, 416, 423, 358, 327, 251, 284, 298, 281, 5, 4, 373, 374, 253, 307, 320,
    321, 425, 427, 411, 421, 313, 18, 321, 405, 406, 320, 404, 405, 315, 16, 17,
    426, 425, 266, 377, 400, 369, 322, 391, 269, 417, 465, 464, 386, 257, 258,
    466, 260, 388, 456, 399, 419, 284, 332, 333, 417, 285, 8, 346, 340, 261,
    413, 441, 285, 327, 460, 328, 355, 371, 329, 392, 439, 438, 382, 341, 256,
    429, 420, 360, 364, 394, 379, 277, 343, 437, 443, 444, 283, 275, 440, 363,
    431, 262, 369, 297, 338, 337, 273, 375, 321, 450, 451, 349, 446, 342, 467,
    293, 334, 282, 458, 461, 462, 276, 353, 383, 308, 324, 325, 276, 300, 293,
    372, 345, 447, 382, 398, 362, 352, 345, 340, 274, 1, 19, 456, 248, 281, 436,
    427, 425, 381, 256, 252, 269, 391, 393, 200, 199, 428, 266, 330, 329, 287,
    273, 422, 250, 462, 328, 258, 286, 384, 265, 353, 342, 387, 259, 257, 424,
    431, 430, 342, 353, 276, 273, 335, 424, 292, 325, 307, 366, 447, 345, 271,
    303, 302, 423, 266, 371, 294, 455, 460, 279, 278, 294, 271, 272, 304, 432,
    434, 427, 272, 407, 408, 394, 430, 431, 395, 369, 400, 334, 333, 299, 351,
    417, 168, 352, 280, 411, 325, 319, 320, 295, 296, 336, 319, 403, 404, 330,
    348, 349, 293, 298, 333, 323, 454, 447, 15, 16, 315, 358, 429, 279, 14, 15,
    316, 285, 336, 9, 329, 349, 350, 374, 380, 252, 318, 402, 403, 6, 197, 419,
    318, 319, 325, 367, 364, 365, 435, 367, 397, 344, 438, 439, 272, 271, 311,
    195, 5, 281, 273, 287, 291, 396, 428, 199, 311, 271, 268, 283, 444, 445,
    373, 254, 339, 263, 466, 249, 282, 334, 296, 449, 347, 346, 264, 447, 454,
    336, 296, 299, 338, 10, 151, 278, 439, 455, 292, 407, 415, 358, 371, 355,
    340, 345, 372, 390, 249, 466, 346, 347, 280, 442, 443, 282, 19, 94, 370,
    441, 442, 295, 248, 419, 197, 263, 255, 359, 440, 275, 274, 300, 383, 368,
    351, 412, 465, 263, 467, 466, 301, 368, 389, 380, 374, 386, 395, 378, 379,
    412, 351, 419, 436, 426, 322, 373, 390, 388, 2, 164, 393, 370, 462, 461,
    164, 0, 267, 302, 11, 12, 374, 373, 387, 268, 12, 13, 293, 300, 301, 446,
    261, 340, 385, 384, 381, 330, 266, 425, 426, 423, 391, 429, 355, 437, 391,
    327, 326, 440, 457, 438, 341, 382, 362, 459, 457, 461, 434, 430, 394, 414,
    463, 362, 396, 369, 262, 354, 461, 457, 316, 403, 402, 315, 404, 403, 314,
    405, 404, 313, 406, 405, 421, 418, 406, 366, 401, 361, 306, 408, 407, 291,
    409, 408, 287, 410, 409, 432, 436, 410, 434, 416, 411, 264, 368, 383, 309,
    438, 457, 352, 376, 401, 274, 275, 4, 421, 428, 262, 294, 327, 358, 433,
    416, 367, 289, 455, 439, 462, 370, 326, 2, 326, 370, 305, 460, 455, 254,
    449, 448, 255, 261, 446, 253, 450, 449, 252, 451, 450, 256, 452, 451, 341,
    453, 452, 413, 464, 463, 441, 413, 414, 258, 442, 441, 257, 443, 442, 259,
    444, 443, 260, 445, 444, 467, 342, 445, 459, 458, 250, 289, 392, 290, 290,
    328, 460, 376, 433, 435, 250, 290, 392, 411, 416, 433, 341, 463, 464, 453,
    464, 465, 357, 465, 412, 343, 412, 399, 360, 363, 440, 437, 399, 456, 420,
    456, 363, 401, 435, 288, 372, 383, 353, 339, 255, 249, 448, 261, 255, 133,
    243, 190, 133, 155, 112, 33, 246, 247, 33, 130, 25, 398, 384, 286, 362, 398,
    414, 362, 463, 341, 263, 359, 467, 263, 249, 255, 466, 467, 260, 75, 60,
    166, 238, 239, 79, 162, 127, 139, 72, 11, 37, 121, 232, 120, 73, 72, 39,
    114, 128, 47, 233, 232, 128, 103, 104, 67, 152, 175, 148, 173, 157, 155,
    119, 118, 101, 74, 73, 40, 107, 9, 108, 49, 48, 131, 32, 194, 211, 184, 74,
    185, 191, 80, 183, 185, 40, 186, 119, 230, 118, 210, 202, 214, 84, 83, 17,
    77, 76, 146, 161, 160, 30, 190, 56, 173, 182, 106, 194, 138, 135, 192, 129,
    203, 98, 54, 21, 68, 5, 51, 4, 145, 144, 23, 90, 77, 91, 207, 205, 187, 83,
    201, 18, 181, 91, 182, 180, 90, 181, 16, 85, 17, 205, 206, 36, 176, 148,
    140, 165, 92, 39, 245, 193, 244, 27, 159, 28, 30, 247, 161, 174, 236, 196,
    103, 54, 104, 55, 193, 8, 111, 117, 31, 221, 189, 55, 240, 98, 99, 142, 126,
    100, 219, 166, 218, 112, 155, 26, 198, 209, 131, 169, 135, 150, 114, 47,
    217, 224, 223, 53, 220, 45, 134, 32, 211, 140, 109, 67, 108, 146, 43, 91,
    231, 230, 120, 113, 226, 247, 105, 63, 52, 241, 238, 242, 124, 46, 156, 95,
    78, 96, 70, 46, 63, 116, 143, 227, 116, 123, 111, 1, 44, 19, 3, 236, 51,
    207, 216, 205, 26, 154, 22, 165, 39, 167, 199, 200, 208, 101, 36, 100, 43,
    57, 202, 242, 20, 99, 56, 28, 157, 124, 35, 113, 29, 160, 27, 211, 204, 210,
    124, 113, 46, 106, 43, 204, 96, 62, 77, 227, 137, 116, 73, 41, 72, 36, 203,
    142, 235, 64, 240, 48, 49, 64, 42, 41, 74, 214, 212, 207, 183, 42, 184, 210,
    169, 211, 140, 170, 176, 104, 105, 69, 193, 122, 168, 50, 123, 187, 89, 96,
    90, 66, 65, 107, 179, 89, 180, 119, 101, 120, 68, 63, 104, 234, 93, 227, 16,
    15, 85, 209, 129, 49, 15, 14, 86, 107, 55, 9, 120, 100, 121, 153, 145, 22,
    178, 88, 179, 197, 6, 196, 89, 88, 96, 135, 138, 136, 138, 215, 172, 218,
    115, 219, 41, 42, 81, 5, 195, 51, 57, 43, 61, 208, 171, 199, 41, 81, 38,
    224, 53, 225, 24, 144, 110, 105, 52, 66, 118, 229, 117, 227, 34, 234, 66,
    107, 69, 10, 109, 151, 219, 48, 235, 183, 62, 191, 142, 129, 126, 116, 111,
    143, 7, 163, 246, 118, 117, 50, 223, 222, 52, 94, 19, 141, 222, 221, 65,
    196, 3, 197, 45, 220, 44, 156, 70, 139, 188, 122, 245, 139, 71, 162, 145,
    153, 159, 149, 170, 150, 122, 188, 196, 206, 216, 92, 163, 144, 161, 164, 2,
    167, 242, 141, 241, 0, 164, 37, 11, 72, 12, 144, 145, 160, 12, 38, 13, 70,
    63, 71, 31, 226, 111, 157, 158, 154, 36, 101, 205, 203, 206, 165, 126, 209,
    217, 98, 165, 97, 237, 220, 218, 237, 239, 241, 210, 214, 169, 140, 171, 32,
    241, 125, 237, 179, 86, 178, 180, 85, 179, 181, 84, 180, 182, 83, 181, 194,
    201, 182, 177, 137, 132, 184, 76, 183, 185, 61, 184, 186, 57, 185, 216, 212,
    186, 192, 214, 187, 139, 34, 156, 218, 79, 237, 147, 123, 177, 45, 44, 4,
    208, 201, 32, 98, 64, 129, 192, 213, 138, 235, 59, 219, 141, 242, 97, 97, 2,
    141, 240, 75, 235, 229, 24, 228, 31, 25, 226, 230, 23, 229, 231, 22, 230,
    232, 26, 231, 233, 112, 232, 244, 189, 243, 189, 221, 190, 222, 28, 221,
    223, 27, 222, 224, 29, 223, 225, 30, 224, 113, 247, 225, 99, 60, 240, 213,
    147, 215, 60, 20, 166, 192, 187, 213, 243, 112, 244, 244, 233, 245, 245,
    128, 188, 188, 114, 174, 134, 131, 220, 174, 217, 236, 236, 198, 134, 215,
    177, 58, 156, 143, 124, 25, 110, 7, 31, 228, 25, 264, 356, 368, 0, 11, 267,
    451, 452, 349, 267, 302, 269, 350, 357, 277, 350, 452, 357, 299, 333, 297,
    396, 175, 377, 381, 384, 382, 280, 347, 330, 269, 303, 270, 151, 9, 337,
    344, 278, 360, 424, 418, 431, 270, 304, 409, 272, 310, 407, 322, 270, 410,
    449, 450, 347, 432, 422, 434, 18, 313, 17, 291, 306, 375, 259, 387, 260,
    424, 335, 418, 434, 364, 416, 391, 423, 327, 301, 251, 298, 275, 281, 4,
    254, 373, 253, 375, 307, 321, 280, 425, 411, 200, 421, 18, 335, 321, 406,
    321, 320, 405, 314, 315, 17, 423, 426, 266, 396, 377, 369, 270, 322, 269,
    413, 417, 464, 385, 386, 258, 248, 456, 419, 298, 284, 333, 168, 417, 8,
    448, 346, 261, 417, 413, 285, 326, 327, 328, 277, 355, 329, 309, 392, 438,
    381, 382, 256, 279, 429, 360, 365, 364, 379, 355, 277, 437, 282, 443, 283,
    281, 275, 363, 395, 431, 369, 299, 297, 337, 335, 273, 321, 348, 450, 349,
    359, 446, 467, 283, 293, 282, 250, 458, 462, 300, 276, 383, 292, 308, 325,
    283, 276, 293, 264, 372, 447, 346, 352, 340, 354, 274, 19, 363, 456, 281,
    426, 436, 425, 380, 381, 252, 267, 269, 393, 421, 200, 428, 371, 266, 329,
    432, 287, 422, 290, 250, 328, 385, 258, 384, 446, 265, 342, 386, 387, 257,
    422, 424, 430, 445, 342, 276, 422, 273, 424, 306, 292, 307, 352, 366, 345,
    268, 271, 302, 358, 423, 371, 327, 294, 460, 331, 279, 294, 303, 271, 304,
    436, 432, 427, 304, 272, 408, 395, 394, 431, 378, 395, 400, 296, 334, 299,
    6, 351, 168, 376, 352, 411, 307, 325, 320, 285, 295, 336, 320, 319, 404,
    329, 330, 349, 334, 293, 333, 366, 323, 447, 316, 15, 315, 331, 358, 279,
    317, 14, 316, 8, 285, 9, 277, 329, 350, 253, 374, 252, 319, 318, 403, 351,
    6, 419, 324, 318, 325, 397, 367, 365, 288, 435, 397, 278, 344, 439, 310,
    272, 311, 248, 195, 281, 375, 273, 291, 175, 396, 199, 312, 311, 268, 276,
    283, 445, 390, 373, 339, 295, 282, 296, 448, 449, 346, 356, 264, 454, 337,
    336, 299, 337, 338, 151, 294, 278, 455, 308, 292, 415, 429, 358, 355, 265,
    340, 372, 388, 390, 466, 352, 346, 280, 295, 442, 282, 354, 19, 370, 285,
    441, 295, 195, 248, 197, 457, 440, 274, 301, 300, 368, 417, 351, 465, 251,
    301, 389, 385, 380, 386, 394, 395, 379, 399, 412, 419, 410, 436, 322, 387,
    373, 388, 326, 2, 393, 354, 370, 461, 393, 164, 267, 268, 302, 12, 386, 374,
    387, 312, 268, 13, 298, 293, 301, 265, 446, 340, 380, 385, 381, 280, 330,
    425, 322, 426, 391, 420, 429, 437, 393, 391, 326, 344, 440, 438, 458, 459,
    461, 364, 434, 394, 428, 396, 262, 274, 354, 457, 317, 316, 402, 316, 315,
    403, 315, 314, 404, 314, 313, 405, 313, 421, 406, 323, 366, 361, 292, 306,
    407, 306, 291, 408, 291, 287, 409, 287, 432, 410, 427, 434, 411, 372, 264,
    383, 459, 309, 457, 366, 352, 401, 1, 274, 4, 418, 421, 262, 331, 294, 358,
    435, 433, 367, 392, 289, 439, 328, 462, 326, 94, 2, 370, 289, 305, 455, 339,
    254, 448, 359, 255, 446, 254, 253, 449, 253, 252, 450, 252, 256, 451, 256,
    341, 452, 414, 413, 463, 286, 441, 414, 286, 258, 441, 258, 257, 442, 257,
    259, 443, 259, 260, 444, 260, 467, 445, 309, 459, 250, 305, 289, 290, 305,
    290, 460, 401, 376, 435, 309, 250, 392, 376, 411, 433, 453, 341, 464, 357,
    453, 465, 343, 357, 412, 437, 343, 399, 344, 360, 440, 420, 437, 456, 360,
    420, 363, 361, 401, 288, 265, 372, 353, 390, 339, 249, 339, 448, 255
};


static int s_face_wo_eyes_tris[] =
{
    127, 34, 139, 11, 0, 37, 232, 231, 120, 72, 37, 39, 128, 121, 47, 232, 121,
    128, 104, 69, 67, 175, 171, 148, 118, 50, 101, 73, 39, 40, 9,
    151, 108, 48, 115, 131, 194, 204, 211, 74, 40, 185, 80, 42, 183, 40, 92,
    186, 230, 229, 118, 202, 212, 214, 83, 18, 17, 76, 61, 146, 160, 29, 30, 56,
    157, 173, 106, 204, 194, 135, 214, 192, 203, 165, 98, 21, 71, 68, 51, 45, 4,
    144, 24, 23, 77, 146, 91, 205, 50, 187, 201, 200, 18, 91, 106, 182, 90, 91,
    181, 85, 84, 17, 206, 203, 36, 148, 171, 140, 92, 40, 39, 193, 189, 244,
    159, 158, 28, 247, 246, 161, 236, 3, 196, 54, 68, 104, 193, 168, 8, 117,
    228, 31, 189, 193, 55, 98, 97, 99, 126, 47, 100, 166, 79, 218, 155, 154, 26,
    209, 49, 131, 135, 136, 150, 47, 126, 217, 223, 52, 53, 45, 51, 134, 211,
    170, 140, 67, 69, 108, 43, 106, 91, 230, 119, 120, 226, 130, 247, 63, 53,
    52, 238, 20, 242, 46, 70, 156, 78, 62, 96, 46, 53, 63, 143, 34, 227,
    123, 117, 111, 44, 125, 19, 236, 134, 51, 216, 206, 205, 154, 153,
    22, 39, 37, 167, 200, 201, 208, 36, 142, 100, 57, 212, 202, 20, 60, 99, 28,
    158, 157, 35, 226, 113, 160, 159, 27, 204, 202, 210, 113, 225, 46, 43, 202,
    204, 62, 76, 77, 137, 123, 116, 41, 38, 72, 203, 129, 142, 64, 98, 240, 49,
    102, 64, 41, 73, 74, 212, 216, 207, 42, 74, 184, 169, 170, 211, 170, 149,
    176, 105, 66, 69, 122, 6, 168, 123, 147, 187, 96, 77, 90, 65, 55, 107, 89,
    90, 180, 101, 100, 120, 63, 105, 104, 93, 137, 227, 15, 86, 85, 129, 102,
    49, 14, 87, 86, 55, 8, 9, 100, 47, 121, 145, 23, 22, 88, 89, 179, 6, 122,
    196, 88, 95, 96, 138, 172, 136, 215, 58, 172, 115, 48, 219, 42, 80, 81, 195,
    3, 51, 43, 146, 61, 171, 175, 199, 81, 82, 38, 53, 46, 225, 144, 163, 110,
    52, 65, 66, 229, 228, 117, 34, 127, 234, 107, 108, 69, 109, 108,
    151, 48, 64, 235, 62, 78, 191, 129, 209, 126, 111, 35, 143,
    117, 123, 50, 222, 65, 52, 19, 125, 141, 221, 55, 65, 3, 195, 197, 25, 7,
    33, 220, 237, 44, 70, 71, 139, 122, 193, 245, 247, 130, 33, 71, 21, 162,
    170, 169, 150, 188, 174, 196, 216, 186, 92, 2,
    97, 167, 141, 125, 241, 164, 167, 37, 72, 38, 12, 38, 82, 13,
    63, 68, 71, 226, 35, 111, 101, 50, 205, 206, 92, 165, 209,
    198, 217, 165, 167, 97, 220, 115, 218, 133, 112, 243, 239, 238, 241, 214,
    135, 169, 190, 173, 133, 171, 208, 32, 125, 44, 237, 86, 87, 178, 85, 86,
    179, 84, 85, 180, 83, 84, 181, 201, 83, 182, 137, 93, 132, 76, 62, 183, 61,
    76, 184, 57, 61, 185, 212, 57, 186, 214, 207, 187, 34, 143, 156, 79, 239,
    237, 123, 137, 177, 44, 1, 4, 201, 194, 32, 64, 102, 129, 213, 215, 138, 59,
    166, 219, 242, 99, 97, 2, 94, 141, 75, 59, 235, 24, 110, 228, 25, 130, 226,
    23, 24, 229, 22, 23, 230, 26, 22, 231, 112, 26, 232, 189, 190, 243, 221, 56,
    190, 28, 56, 221, 27, 28, 222, 29, 27, 223, 30, 29, 224, 247, 30, 225, 238,
    79, 20, 166, 59, 75, 60, 75, 240, 147, 177, 215, 20, 79, 166, 187, 147, 213,
    112, 233, 244, 233, 128, 245, 128, 114, 188, 114, 217, 174, 131, 115, 220,
    217, 198, 236, 198, 131, 134, 177, 132, 58, 143, 35, 124, 110, 163, 7, 228,
    110, 25, 356, 389, 368, 11, 302, 267, 452, 350, 349, 302, 303, 269, 357,
    343, 277, 452, 453, 357, 333, 332, 297, 175, 152, 377, 347,
    348, 330, 303, 304, 270, 9, 336, 337, 278, 279, 360, 418, 262, 431, 304,
    408, 409, 310, 415, 407, 270, 409, 410, 450, 348, 347, 422, 430, 434, 313,
    314, 17, 306, 307, 375, 387, 388, 260, 286, 414, 398, 335, 406, 418, 364,
    367, 416, 423, 358, 327, 251, 284, 298, 281, 5, 4, 373, 374, 253, 307, 320,
    321, 425, 427, 411, 421, 313, 18, 321, 405, 406, 320, 404, 405, 315, 16, 17,
    426, 425, 266, 377, 400, 369, 322, 391, 269, 417, 465, 464, 386, 257, 258,
    466, 260, 388, 456, 399, 419, 284, 332, 333, 417, 285, 8, 346, 340, 261,
    413, 441, 285, 327, 460, 328, 355, 371, 329, 392, 439, 438, 382, 341, 256,
    429, 420, 360, 364, 394, 379, 277, 343, 437, 443, 444, 283, 275, 440, 363,
    431, 262, 369, 297, 338, 337, 273, 375, 321, 450, 451, 349, 446, 342, 467,
    293, 334, 282, 458, 461, 462, 276, 353, 383, 308, 324, 325, 276, 300, 293,
    372, 345, 447, 352, 345, 340, 274, 1, 19, 456, 248, 281, 436,
    427, 425, 381, 256, 252, 269, 391, 393, 200, 199, 428, 266, 330, 329, 287,
    273, 422, 250, 462, 328, 258, 286, 384, 265, 353, 342, 387, 259, 257, 424,
    431, 430, 342, 353, 276, 273, 335, 424, 292, 325, 307, 366, 447, 345, 271,
    303, 302, 423, 266, 371, 294, 455, 460, 279, 278, 294, 271, 272, 304, 432,
    434, 427, 272, 407, 408, 394, 430, 431, 395, 369, 400, 334, 333, 299, 351,
    417, 168, 352, 280, 411, 325, 319, 320, 295, 296, 336, 319, 403, 404, 330,
    348, 349, 293, 298, 333, 323, 454, 447, 15, 16, 315, 358, 429, 279, 14, 15,
    316, 285, 336, 9, 329, 349, 350, 374, 380, 252, 318, 402, 403, 6, 197, 419,
    318, 319, 325, 367, 364, 365, 435, 367, 397, 344, 438, 439, 272, 271, 311,
    195, 5, 281, 273, 287, 291, 396, 428, 199, 311, 271, 268, 283, 444, 445,
    373, 254, 339, 282, 334, 296, 449, 347, 346, 264, 447, 454,
    336, 296, 299, 338, 10, 151, 278, 439, 455, 292, 407, 415, 358, 371, 355,
    340, 345, 372, 346, 347, 280, 442, 443, 282, 19, 94, 370,
    441, 442, 295, 248, 419, 197, 263, 255, 359, 440, 275, 274, 300, 383, 368,
    351, 412, 465, 263, 467, 466, 301, 368, 389, 395, 378, 379,
    412, 351, 419, 436, 426, 322, 2, 164, 393, 370, 462, 461,
    164, 0, 267, 302, 11, 12, 268, 12, 13, 293, 300, 301, 446,
    261, 340, 330, 266, 425, 426, 423, 391, 429, 355, 437, 391,
    327, 326, 440, 457, 438, 341, 382, 362, 459, 457, 461, 434, 430, 394, 414,
    463, 362, 396, 369, 262, 354, 461, 457, 316, 403, 402, 315, 404, 403, 314,
    405, 404, 313, 406, 405, 421, 418, 406, 366, 401, 361, 306, 408, 407, 291,
    409, 408, 287, 410, 409, 432, 436, 410, 434, 416, 411, 264, 368, 383, 309,
    438, 457, 352, 376, 401, 274, 275, 4, 421, 428, 262, 294, 327, 358, 433,
    416, 367, 289, 455, 439, 462, 370, 326, 2, 326, 370, 305, 460, 455, 254,
    449, 448, 255, 261, 446, 253, 450, 449, 252, 451, 450, 256, 452, 451, 341,
    453, 452, 413, 464, 463, 441, 413, 414, 258, 442, 441, 257, 443, 442, 259,
    444, 443, 260, 445, 444, 467, 342, 445, 459, 458, 250, 289, 392, 290, 290,
    328, 460, 376, 433, 435, 250, 290, 392, 411, 416, 433, 341, 463, 464, 453,
    464, 465, 357, 465, 412, 343, 412, 399, 360, 363, 440, 437, 399, 456, 420,
    456, 363, 401, 435, 288, 372, 383, 353, 339, 255, 249, 448, 261, 255, 133,
    243, 190, 133, 155, 112, 33, 246, 247, 33, 130, 25, 398, 384, 286, 362, 398,
    414, 362, 463, 341, 263, 359, 467, 263, 249, 255, 466, 467, 260, 75, 60,
    166, 238, 239, 79, 162, 127, 139, 72, 11, 37, 121, 232, 120, 73, 72, 39,
    114, 128, 47, 233, 232, 128, 103, 104, 67, 152, 175, 148,
    119, 118, 101, 74, 73, 40, 107, 9, 108, 49, 48, 131, 32, 194, 211, 184, 74,
    185, 191, 80, 183, 185, 40, 186, 119, 230, 118, 210, 202, 214, 84, 83, 17,
    77, 76, 146, 161, 160, 30, 190, 56, 173, 182, 106, 194, 138, 135, 192, 129,
    203, 98, 54, 21, 68, 5, 51, 4, 145, 144, 23, 90, 77, 91, 207, 205, 187, 83,
    201, 18, 181, 91, 182, 180, 90, 181, 16, 85, 17, 205, 206, 36, 176, 148,
    140, 165, 92, 39, 245, 193, 244, 27, 159, 28, 30, 247, 161, 174, 236, 196,
    103, 54, 104, 55, 193, 8, 111, 117, 31, 221, 189, 55, 240, 98, 99, 142, 126,
    100, 219, 166, 218, 112, 155, 26, 198, 209, 131, 169, 135, 150, 114, 47,
    217, 224, 223, 53, 220, 45, 134, 32, 211, 140, 109, 67, 108, 146, 43, 91,
    231, 230, 120, 113, 226, 247, 105, 63, 52, 241, 238, 242, 124, 46, 156, 95,
    78, 96, 70, 46, 63, 116, 143, 227, 116, 123, 111, 1, 44, 19, 3, 236, 51,
    207, 216, 205, 26, 154, 22, 165, 39, 167, 199, 200, 208, 101, 36, 100, 43,
    57, 202, 242, 20, 99, 56, 28, 157, 124, 35, 113, 29, 160, 27, 211, 204, 210,
    124, 113, 46, 106, 43, 204, 96, 62, 77, 227, 137, 116, 73, 41, 72, 36, 203,
    142, 235, 64, 240, 48, 49, 64, 42, 41, 74, 214, 212, 207, 183, 42, 184, 210,
    169, 211, 140, 170, 176, 104, 105, 69, 193, 122, 168, 50, 123, 187, 89, 96,
    90, 66, 65, 107, 179, 89, 180, 119, 101, 120, 68, 63, 104, 234, 93, 227, 16,
    15, 85, 209, 129, 49, 15, 14, 86, 107, 55, 9, 120, 100, 121, 153, 145, 22,
    178, 88, 179, 197, 6, 196, 89, 88, 96, 135, 138, 136, 138, 215, 172, 218,
    115, 219, 41, 42, 81, 5, 195, 51, 57, 43, 61, 208, 171, 199, 41, 81, 38,
    224, 53, 225, 24, 144, 110, 105, 52, 66, 118, 229, 117, 227, 34, 234, 66,
    107, 69, 10, 109, 151, 219, 48, 235, 183, 62, 191, 142, 129, 126, 116, 111,
    143, 118, 117, 50, 223, 222, 52, 94, 19, 141, 222, 221, 65,
    196, 3, 197, 45, 220, 44, 156, 70, 139, 188, 122, 245, 139, 71, 162,
    149, 170, 150, 122, 188, 196, 206, 216, 92, 164, 2,
    167, 242, 141, 241, 0, 164, 37, 11, 72, 12, 12, 38, 13, 70,
    63, 71, 31, 226, 111, 36, 101, 205, 203, 206, 165, 126, 209,
    217, 98, 165, 97, 237, 220, 218, 237, 239, 241, 210, 214, 169, 140, 171, 32,
    241, 125, 237, 179, 86, 178, 180, 85, 179, 181, 84, 180, 182, 83, 181, 194,
    201, 182, 177, 137, 132, 184, 76, 183, 185, 61, 184, 186, 57, 185, 216, 212,
    186, 192, 214, 187, 139, 34, 156, 218, 79, 237, 147, 123, 177, 45, 44, 4,
    208, 201, 32, 98, 64, 129, 192, 213, 138, 235, 59, 219, 141, 242, 97, 97, 2,
    141, 240, 75, 235, 229, 24, 228, 31, 25, 226, 230, 23, 229, 231, 22, 230,
    232, 26, 231, 233, 112, 232, 244, 189, 243, 189, 221, 190, 222, 28, 221,
    223, 27, 222, 224, 29, 223, 225, 30, 224, 113, 247, 225, 99, 60, 240, 213,
    147, 215, 60, 20, 166, 192, 187, 213, 243, 112, 244, 244, 233, 245, 245,
    128, 188, 188, 114, 174, 134, 131, 220, 174, 217, 236, 236, 198, 134, 215,
    177, 58, 156, 143, 124, 25, 110, 7, 31, 228, 25, 264, 356, 368, 0, 11, 267,
    451, 452, 349, 267, 302, 269, 350, 357, 277, 350, 452, 357, 299, 333, 297,
    396, 175, 377, 280, 347, 330, 269, 303, 270, 151, 9, 337,
    344, 278, 360, 424, 418, 431, 270, 304, 409, 272, 310, 407, 322, 270, 410,
    449, 450, 347, 432, 422, 434, 18, 313, 17, 291, 306, 375, 259, 387, 260,
    424, 335, 418, 434, 364, 416, 391, 423, 327, 301, 251, 298, 275, 281, 4,
    254, 373, 253, 375, 307, 321, 280, 425, 411, 200, 421, 18, 335, 321, 406,
    321, 320, 405, 314, 315, 17, 423, 426, 266, 396, 377, 369, 270, 322, 269,
    413, 417, 464, 385, 386, 258, 248, 456, 419, 298, 284, 333, 168, 417, 8,
    448, 346, 261, 417, 413, 285, 326, 327, 328, 277, 355, 329, 309, 392, 438,
    381, 382, 256, 279, 429, 360, 365, 364, 379, 355, 277, 437, 282, 443, 283,
    281, 275, 363, 395, 431, 369, 299, 297, 337, 335, 273, 321, 348, 450, 349,
    359, 446, 467, 283, 293, 282, 250, 458, 462, 300, 276, 383, 292, 308, 325,
    283, 276, 293, 264, 372, 447, 346, 352, 340, 354, 274, 19, 363, 456, 281,
    426, 436, 425, 380, 381, 252, 267, 269, 393, 421, 200, 428, 371, 266, 329,
    432, 287, 422, 290, 250, 328, 385, 258, 384, 446, 265, 342, 386, 387, 257,
    422, 424, 430, 445, 342, 276, 422, 273, 424, 306, 292, 307, 352, 366, 345,
    268, 271, 302, 358, 423, 371, 327, 294, 460, 331, 279, 294, 303, 271, 304,
    436, 432, 427, 304, 272, 408, 395, 394, 431, 378, 395, 400, 296, 334, 299,
    6, 351, 168, 376, 352, 411, 307, 325, 320, 285, 295, 336, 320, 319, 404,
    329, 330, 349, 334, 293, 333, 366, 323, 447, 316, 15, 315, 331, 358, 279,
    317, 14, 316, 8, 285, 9, 277, 329, 350, 253, 374, 252, 319, 318, 403, 351,
    6, 419, 324, 318, 325, 397, 367, 365, 288, 435, 397, 278, 344, 439, 310,
    272, 311, 248, 195, 281, 375, 273, 291, 175, 396, 199, 312, 311, 268, 276,
    283, 445, 390, 373, 339, 295, 282, 296, 448, 449, 346, 356, 264, 454, 337,
    336, 299, 337, 338, 151, 294, 278, 455, 308, 292, 415, 429, 358, 355, 265,
    340, 372, 352, 346, 280, 295, 442, 282, 354, 19, 370, 285,
    441, 295, 195, 248, 197, 457, 440, 274, 301, 300, 368, 417, 351, 465, 251,
    301, 389, 394, 395, 379, 399, 412, 419, 410, 436, 322,
    326, 2, 393, 354, 370, 461, 393, 164, 267, 268, 302, 12,
    312, 268, 13, 298, 293, 301, 265, 446, 340, 280, 330,
    425, 322, 426, 391, 420, 429, 437, 393, 391, 326, 344, 440, 438, 458, 459,
    461, 364, 434, 394, 428, 396, 262, 274, 354, 457, 317, 316, 402, 316, 315,
    403, 315, 314, 404, 314, 313, 405, 313, 421, 406, 323, 366, 361, 292, 306,
    407, 306, 291, 408, 291, 287, 409, 287, 432, 410, 427, 434, 411, 372, 264,
    383, 459, 309, 457, 366, 352, 401, 1, 274, 4, 418, 421, 262, 331, 294, 358,
    435, 433, 367, 392, 289, 439, 328, 462, 326, 94, 2, 370, 289, 305, 455, 339,
    254, 448, 359, 255, 446, 254, 253, 449, 253, 252, 450, 252, 256, 451, 256,
    341, 452, 414, 413, 463, 286, 441, 414, 286, 258, 441, 258, 257, 442, 257,
    259, 443, 259, 260, 444, 260, 467, 445, 309, 459, 250, 305, 289, 290, 305,
    290, 460, 401, 376, 435, 309, 250, 392, 376, 411, 433, 453, 341, 464, 357,
    453, 465, 343, 357, 412, 437, 343, 399, 344, 360, 440, 420, 437, 456, 360,
    420, 363, 361, 401, 288, 265, 372, 353, 390, 339, 249, 339, 448, 255
};


int *
get_facemesh_tri_indicies (int *num_tris, int flags)
{
    if (flags == 0)
    {
        *num_tris = sizeof (s_face_tris) / sizeof (int);
        return s_face_tris;
    }
    else
    {
        *num_tris = sizeof (s_face_wo_eyes_tris) / sizeof (int);
        return s_face_wo_eyes_tris;
    }
}







static float s_face_uv[] =
{
  0.499976992607117, 0.652534008026123,
  0.500025987625122, 0.547487020492554,
  0.499974012374878, 0.602371990680695,
  0.482113003730774, 0.471979022026062,
  0.500150978565216, 0.527155995368958,
  0.499909996986389, 0.498252987861633,
  0.499523013830185, 0.40106201171875,
  0.289712011814117, 0.380764007568359,
  0.499954998493195, 0.312398016452789,
  0.499987006187439, 0.269918978214264,
  0.500023007392883, 0.107050001621246,
  0.500023007392883, 0.666234016418457,
  0.5000159740448, 0.679224014282227,
  0.500023007392883, 0.692348003387451,
  0.499976992607117, 0.695277988910675,
  0.499976992607117, 0.70593398809433,
  0.499976992607117, 0.719385027885437,
  0.499976992607117, 0.737019002437592,
  0.499967992305756, 0.781370997428894,
  0.499816000461578, 0.562981009483337,
  0.473773002624512, 0.573909997940063,
  0.104906998574734, 0.254140973091125,
  0.365929991006851, 0.409575998783112,
  0.338757991790771, 0.41302502155304,
  0.311120003461838, 0.409460008144379,
  0.274657994508743, 0.389131009578705,
  0.393361985683441, 0.403706014156342,
  0.345234006643295, 0.344011008739471,
  0.370094001293182, 0.346076011657715,
  0.319321990013123, 0.347265005111694,
  0.297903001308441, 0.353591024875641,
  0.24779200553894, 0.410809993743896,
  0.396889001131058, 0.842755019664764,
  0.280097991228104, 0.375599980354309,
  0.106310002505779, 0.399955987930298,
  0.2099249958992, 0.391353011131287,
  0.355807989835739, 0.534406006336212,
  0.471751004457474, 0.65040397644043,
  0.474155008792877, 0.680191993713379,
  0.439785003662109, 0.657229006290436,
  0.414617002010345, 0.66654098033905,
  0.450374007225037, 0.680860996246338,
  0.428770989179611, 0.682690978050232,
  0.374971002340317, 0.727805018424988,
  0.486716985702515, 0.547628998756409,
  0.485300987958908, 0.527395009994507,
  0.257764995098114, 0.314490020275116,
  0.401223003864288, 0.455172002315521,
  0.429818987846375, 0.548614978790283,
  0.421351999044418, 0.533740997314453,
  0.276895999908447, 0.532056987285614,
  0.483370006084442, 0.499586999416351,
  0.33721199631691, 0.282882988452911,
  0.296391993761063, 0.293242990970612,
  0.169294998049736, 0.193813979625702,
  0.447580009698868, 0.302609980106354,
  0.392390012741089, 0.353887975215912,
  0.354490011930466, 0.696784019470215,
  0.067304998636246, 0.730105042457581,
  0.442739009857178, 0.572826027870178,
  0.457098007202148, 0.584792017936707,
  0.381974011659622, 0.694710969924927,
  0.392388999462128, 0.694203019142151,
  0.277076005935669, 0.271932005882263,
  0.422551989555359, 0.563233017921448,
  0.385919004678726, 0.281364023685455,
  0.383103013038635, 0.255840003490448,
  0.331431001424789, 0.119714021682739,
  0.229923993349075, 0.232002973556519,
  0.364500999450684, 0.189113974571228,
  0.229622006416321, 0.299540996551514,
  0.173287004232407, 0.278747975826263,
  0.472878992557526, 0.666198015213013,
  0.446828007698059, 0.668527007102966,
  0.422762006521225, 0.673889994621277,
  0.445307999849319, 0.580065965652466,
  0.388103008270264, 0.693961024284363,
  0.403039008378983, 0.706539988517761,
  0.403629004955292, 0.693953037261963,
  0.460041999816895, 0.557139039039612,
  0.431158006191254, 0.692366003990173,
  0.452181994915009, 0.692366003990173,
  0.475387006998062, 0.692366003990173,
  0.465828001499176, 0.779190003871918,
  0.472328990697861, 0.736225962638855,
  0.473087012767792, 0.717857003211975,
  0.473122000694275, 0.704625964164734,
  0.473033010959625, 0.695277988910675,
  0.427942007780075, 0.695277988910675,
  0.426479011774063, 0.703539967536926,
  0.423162013292313, 0.711845993995667,
  0.4183090031147, 0.720062971115112,
  0.390094995498657, 0.639572978019714,
  0.013953999616206, 0.560034036636353,
  0.499913990497589, 0.58014702796936,
  0.413199990987778, 0.69539999961853,
  0.409626007080078, 0.701822996139526,
  0.468080013990402, 0.601534962654114,
  0.422728985548019, 0.585985004901886,
  0.463079988956451, 0.593783974647522,
  0.37211999297142, 0.47341400384903,
  0.334562003612518, 0.496073007583618,
  0.411671012639999, 0.546965003013611,
  0.242175996303558, 0.14767599105835,
  0.290776997804642, 0.201445996761322,
  0.327338010072708, 0.256527006626129,
  0.399509996175766, 0.748921036720276,
  0.441727995872498, 0.261676013469696,
  0.429764986038208, 0.187834024429321,
  0.412198007106781, 0.108901023864746,
  0.288955003023148, 0.398952007293701,
  0.218936994671822, 0.435410976409912,
  0.41278201341629, 0.398970007896423,
  0.257135003805161, 0.355440020561218,
  0.427684992551804, 0.437960982322693,
  0.448339998722076, 0.536936044692993,
  0.178560003638268, 0.45755398273468,
  0.247308000922203, 0.457193970680237,
  0.286267012357712, 0.467674970626831,
  0.332827985286713, 0.460712015628815,
  0.368755996227264, 0.447206974029541,
  0.398963987827301, 0.432654976844788,
  0.476410001516342, 0.405806005001068,
  0.189241006970406, 0.523923993110657,
  0.228962004184723, 0.348950982093811,
  0.490725994110107, 0.562400996685028,
  0.404670000076294, 0.485132992267609,
  0.019469000399113, 0.401564002037048,
  0.426243007183075, 0.420431017875671,
  0.396993011236191, 0.548797011375427,
  0.266469985246658, 0.376977026462555,
  0.439121007919312, 0.51895797252655,
  0.032313998788595, 0.644356966018677,
  0.419054001569748, 0.387154996395111,
  0.462783008813858, 0.505746960639954,
  0.238978996872902, 0.779744982719421,
  0.198220998048782, 0.831938028335571,
  0.107550002634525, 0.540755033493042,
  0.183610007166862, 0.740257024765015,
  0.134409993886948, 0.333683013916016,
  0.385764002799988, 0.883153975009918,
  0.490967005491257, 0.579378008842468,
  0.382384985685349, 0.508572995662689,
  0.174399003386497, 0.397670984268188,
  0.318785011768341, 0.39623498916626,
  0.343364000320435, 0.400596976280212,
  0.396100014448166, 0.710216999053955,
  0.187885001301765, 0.588537991046906,
  0.430987000465393, 0.944064974784851,
  0.318993002176285, 0.898285031318665,
  0.266247987747192, 0.869701027870178,
  0.500023007392883, 0.190576016902924,
  0.499976992607117, 0.954452991485596,
  0.366169989109039, 0.398822009563446,
  0.393207013607025, 0.39553701877594,
  0.410373002290726, 0.391080021858215,
  0.194993004202843, 0.342101991176605,
  0.388664990663528, 0.362284004688263,
  0.365961998701096, 0.355970978736877,
  0.343364000320435, 0.355356991291046,
  0.318785011768341, 0.35834002494812,
  0.301414996385574, 0.363156020641327,
  0.058132998645306, 0.319076001644135,
  0.301414996385574, 0.387449026107788,
  0.499987989664078, 0.618434011936188,
  0.415838003158569, 0.624195992946625,
  0.445681989192963, 0.566076993942261,
  0.465844005346298, 0.620640993118286,
  0.49992299079895, 0.351523995399475,
  0.288718998432159, 0.819945991039276,
  0.335278987884521, 0.852819979190826,
  0.440512001514435, 0.902418971061707,
  0.128294005990028, 0.791940987110138,
  0.408771991729736, 0.373893976211548,
  0.455606997013092, 0.451801002025604,
  0.499877005815506, 0.908990025520325,
  0.375436991453171, 0.924192011356354,
  0.11421000212431, 0.615022003650665,
  0.448662012815475, 0.695277988910675,
  0.4480200111866, 0.704632043838501,
  0.447111994028091, 0.715808033943176,
  0.444831997156143, 0.730794012546539,
  0.430011987686157, 0.766808986663818,
  0.406787008047104, 0.685672998428345,
  0.400738000869751, 0.681069016456604,
  0.392399996519089, 0.677703022956848,
  0.367855995893478, 0.663918972015381,
  0.247923001646996, 0.601333022117615,
  0.452769994735718, 0.420849978923798,
  0.43639200925827, 0.359887003898621,
  0.416164010763168, 0.368713974952698,
  0.413385987281799, 0.692366003990173,
  0.228018000721931, 0.683571994304657,
  0.468268007040024, 0.352671027183533,
  0.411361992359161, 0.804327011108398,
  0.499989002943039, 0.469825029373169,
  0.479153990745544, 0.442654013633728,
  0.499974012374878, 0.439637005329132,
  0.432112008333206, 0.493588984012604,
  0.499886006116867, 0.866917014122009,
  0.49991300702095, 0.821729004383087,
  0.456548988819122, 0.819200992584229,
  0.344549000263214, 0.745438992977142,
  0.37890899181366, 0.574010014533997,
  0.374292999505997, 0.780184984207153,
  0.319687992334366, 0.570737957954407,
  0.357154995203018, 0.604269981384277,
  0.295284003019333, 0.621580958366394,
  0.447750002145767, 0.862477004528046,
  0.410986006259918, 0.508723020553589,
  0.31395098567009, 0.775308012962341,
  0.354128003120422, 0.812552988529205,
  0.324548006057739, 0.703992962837219,
  0.189096003770828, 0.646299958229065,
  0.279776990413666, 0.71465802192688,
  0.1338230073452, 0.682700991630554,
  0.336768001317978, 0.644733011722565,
  0.429883986711502, 0.466521978378296,
  0.455527991056442, 0.548622965812683,
  0.437114000320435, 0.558896005153656,
  0.467287987470627, 0.529924988746643,
  0.414712011814117, 0.335219979286194,
  0.37704598903656, 0.322777986526489,
  0.344107985496521, 0.320150971412659,
  0.312875986099243, 0.32233202457428,
  0.283526003360748, 0.333190023899078,
  0.241245999932289, 0.382785975933075,
  0.102986000478268, 0.468762993812561,
  0.267612010240555, 0.424560010433197,
  0.297879010438919, 0.433175981044769,
  0.333433985710144, 0.433878004550934,
  0.366427004337311, 0.426115989685059,
  0.396012008190155, 0.416696012020111,
  0.420121014118195, 0.41022801399231,
  0.007561000064015, 0.480777025222778,
  0.432949006557465, 0.569517970085144,
  0.458638995885849, 0.479089021682739,
  0.473466008901596, 0.545744001865387,
  0.476087987422943, 0.563830018043518,
  0.468472003936768, 0.555056989192963,
  0.433990985155106, 0.582361996173859,
  0.483518004417419, 0.562983989715576,
  0.482482999563217, 0.57784903049469,
  0.42645001411438, 0.389798998832703,
  0.438998997211456, 0.39649498462677,
  0.450067013502121, 0.400434017181396,
  0.289712011814117, 0.368252992630005,
  0.276670008897781, 0.363372981548309,
  0.517862021923065, 0.471948027610779,
  0.710287988185883, 0.380764007568359,
  0.526226997375488, 0.573909997940063,
  0.895093023777008, 0.254140973091125,
  0.634069979190826, 0.409575998783112,
  0.661242008209229, 0.41302502155304,
  0.688880026340485, 0.409460008144379,
  0.725341975688934, 0.389131009578705,
  0.606630027294159, 0.40370500087738,
  0.654766023159027, 0.344011008739471,
  0.629905998706818, 0.346076011657715,
  0.680678009986877, 0.347265005111694,
  0.702096998691559, 0.353591024875641,
  0.75221198797226, 0.410804986953735,
  0.602918028831482, 0.842862963676453,
  0.719901978969574, 0.375599980354309,
  0.893692970275879, 0.399959981441498,
  0.790081977844238, 0.391354024410248,
  0.643998026847839, 0.534487962722778,
  0.528249025344849, 0.65040397644043,
  0.525849997997284, 0.680191040039062,
  0.560214996337891, 0.657229006290436,
  0.585384011268616, 0.66654098033905,
  0.549625992774963, 0.680860996246338,
  0.57122802734375, 0.682691991329193,
  0.624852001667023, 0.72809898853302,
  0.513050019741058, 0.547281980514526,
  0.51509702205658, 0.527251958847046,
  0.742246985435486, 0.314507007598877,
  0.598631024360657, 0.454979002475739,
  0.570338010787964, 0.548575043678284,
  0.578631997108459, 0.533622980117798,
  0.723087012767792, 0.532054007053375,
  0.516445994377136, 0.499638974666595,
  0.662801027297974, 0.282917976379395,
  0.70362401008606, 0.293271005153656,
  0.830704987049103, 0.193813979625702,
  0.552385985851288, 0.302568018436432,
  0.607609987258911, 0.353887975215912,
  0.645429015159607, 0.696707010269165,
  0.932694971561432, 0.730105042457581,
  0.557260990142822, 0.572826027870178,
  0.542901992797852, 0.584792017936707,
  0.6180260181427, 0.694710969924927,
  0.607590973377228, 0.694203019142151,
  0.722943007946014, 0.271963000297546,
  0.577413976192474, 0.563166975975037,
  0.614082992076874, 0.281386971473694,
  0.616907000541687, 0.255886018276215,
  0.668509006500244, 0.119913995265961,
  0.770092010498047, 0.232020974159241,
  0.635536015033722, 0.189248979091644,
  0.77039098739624, 0.299556016921997,
  0.826722025871277, 0.278755009174347,
  0.527121007442474, 0.666198015213013,
  0.553171992301941, 0.668527007102966,
  0.577238023281097, 0.673889994621277,
  0.554691970348358, 0.580065965652466,
  0.611896991729736, 0.693961024284363,
  0.59696102142334, 0.706539988517761,
  0.596370995044708, 0.693953037261963,
  0.539958000183105, 0.557139039039612,
  0.568841993808746, 0.692366003990173,
  0.547818005084991, 0.692366003990173,
  0.52461302280426, 0.692366003990173,
  0.534089982509613, 0.779141008853912,
  0.527670979499817, 0.736225962638855,
  0.526912987232208, 0.717857003211975,
  0.526877999305725, 0.704625964164734,
  0.526966989040375, 0.695277988910675,
  0.572058022022247, 0.695277988910675,
  0.573521018028259, 0.703539967536926,
  0.57683801651001, 0.711845993995667,
  0.581691026687622, 0.720062971115112,
  0.609944999217987, 0.639909982681274,
  0.986046016216278, 0.560034036636353,
  0.5867999792099, 0.69539999961853,
  0.590372025966644, 0.701822996139526,
  0.531915009021759, 0.601536989212036,
  0.577268004417419, 0.585934996604919,
  0.536915004253387, 0.593786001205444,
  0.627542972564697, 0.473352015018463,
  0.665585994720459, 0.495950996875763,
  0.588353991508484, 0.546862006187439,
  0.757824003696442, 0.14767599105835,
  0.709249973297119, 0.201507985591888,
  0.672684013843536, 0.256581008434296,
  0.600408971309662, 0.74900496006012,
  0.55826598405838, 0.261672019958496,
  0.570303976535797, 0.187870979309082,
  0.588165998458862, 0.109044015407562,
  0.711045026779175, 0.398952007293701,
  0.781069993972778, 0.435405015945435,
  0.587247014045715, 0.398931980133057,
  0.742869973182678, 0.355445981025696,
  0.572156012058258, 0.437651991844177,
  0.55186802148819, 0.536570012569427,
  0.821442008018494, 0.457556009292603,
  0.752701997756958, 0.457181990146637,
  0.71375697851181, 0.467626988887787,
  0.66711300611496, 0.460672974586487,
  0.631101012229919, 0.447153985500336,
  0.6008620262146, 0.432473003864288,
  0.523481011390686, 0.405627012252808,
  0.810747981071472, 0.523926019668579,
  0.771045982837677, 0.348959028720856,
  0.509127020835876, 0.562718033790588,
  0.595292985439301, 0.485023975372314,
  0.980530977249146, 0.401564002037048,
  0.573499977588654, 0.420000016689301,
  0.602994978427887, 0.548687994480133,
  0.733529984951019, 0.376977026462555,
  0.560611009597778, 0.519016981124878,
  0.967685997486115, 0.644356966018677,
  0.580985009670258, 0.387160003185272,
  0.537728011608124, 0.505385041236877,
  0.760966002941132, 0.779752969741821,
  0.801778972148895, 0.831938028335571,
  0.892440974712372, 0.54076099395752,
  0.816350996494293, 0.740260004997253,
  0.865594983100891, 0.333687007427216,
  0.614073991775513, 0.883246004581451,
  0.508952975273132, 0.579437971115112,
  0.617941975593567, 0.508316040039062,
  0.825608015060425, 0.397674977779388,
  0.681214988231659, 0.39623498916626,
  0.656635999679565, 0.400596976280212,
  0.603900015354156, 0.710216999053955,
  0.81208598613739, 0.588539004325867,
  0.56801301240921, 0.944564998149872,
  0.681007981300354, 0.898285031318665,
  0.733752012252808, 0.869701027870178,
  0.633830010890961, 0.398822009563446,
  0.606792986392975, 0.39553701877594,
  0.589659988880157, 0.391062021255493,
  0.805015981197357, 0.342108011245728,
  0.611334979534149, 0.362284004688263,
  0.634037971496582, 0.355970978736877,
  0.656635999679565, 0.355356991291046,
  0.681214988231659, 0.35834002494812,
  0.698584973812103, 0.363156020641327,
  0.941866993904114, 0.319076001644135,
  0.698584973812103, 0.387449026107788,
  0.584177017211914, 0.624107003211975,
  0.554318010807037, 0.566076993942261,
  0.534153997898102, 0.62064003944397,
  0.711217999458313, 0.819975018501282,
  0.664629995822906, 0.852871000766754,
  0.559099972248077, 0.902631998062134,
  0.871706008911133, 0.791940987110138,
  0.591234028339386, 0.373893976211548,
  0.544341027736664, 0.451583981513977,
  0.624562978744507, 0.924192011356354,
  0.88577002286911, 0.615028977394104,
  0.551338016986847, 0.695277988910675,
  0.551980018615723, 0.704632043838501,
  0.552887976169586, 0.715808033943176,
  0.555167973041534, 0.730794012546539,
  0.569944024085999, 0.767035007476807,
  0.593203008174896, 0.685675978660583,
  0.599261999130249, 0.681069016456604,
  0.607599973678589, 0.677703022956848,
  0.631937980651855, 0.663500010967255,
  0.752032995223999, 0.601315021514893,
  0.547226011753082, 0.420395016670227,
  0.563543975353241, 0.359827995300293,
  0.583841025829315, 0.368713974952698,
  0.586614012718201, 0.692366003990173,
  0.771915018558502, 0.683578014373779,
  0.531597018241882, 0.352482974529266,
  0.588370978832245, 0.804440975189209,
  0.52079701423645, 0.442565023899078,
  0.567984998226166, 0.493479013442993,
  0.543282985687256, 0.819254994392395,
  0.655317008495331, 0.745514988899231,
  0.621008992195129, 0.574018001556396,
  0.625559985637665, 0.78031200170517,
  0.680198013782501, 0.570719003677368,
  0.64276397228241, 0.604337990283966,
  0.704662978649139, 0.621529996395111,
  0.552012026309967, 0.862591981887817,
  0.589071989059448, 0.508637011051178,
  0.685944974422455, 0.775357007980347,
  0.645735025405884, 0.812640011310577,
  0.675342977046967, 0.703978002071381,
  0.810858011245728, 0.646304965019226,
  0.72012197971344, 0.714666962623596,
  0.866151988506317, 0.682704985141754,
  0.663187026977539, 0.644596993923187,
  0.570082008838654, 0.466325998306274,
  0.544561982154846, 0.548375964164734,
  0.562758982181549, 0.558784961700439,
  0.531987011432648, 0.530140042304993,
  0.585271000862122, 0.335177004337311,
  0.622952997684479, 0.32277899980545,
  0.655896008014679, 0.320163011550903,
  0.687132000923157, 0.322345972061157,
  0.716481983661652, 0.333200991153717,
  0.758756995201111, 0.382786989212036,
  0.897013008594513, 0.468769013881683,
  0.732392013072968, 0.424547016620636,
  0.70211398601532, 0.433162987232208,
  0.66652500629425, 0.433866024017334,
  0.633504986763, 0.426087975502014,
  0.603875994682312, 0.416586995124817,
  0.579657971858978, 0.409945011138916,
  0.992439985275269, 0.480777025222778,
  0.567192018032074, 0.569419980049133,
  0.54136598110199, 0.478899002075195,
  0.526564002037048, 0.546118021011353,
  0.523913025856018, 0.563830018043518,
  0.531529009342194, 0.555056989192963,
  0.566035985946655, 0.582329034805298,
  0.51631098985672, 0.563053965568542,
  0.5174720287323, 0.577877044677734,
  0.573594987392426, 0.389806985855103,
  0.560697972774506, 0.395331978797913,
  0.549755990505219, 0.399751007556915,
  0.710287988185883, 0.368252992630005,
  0.723330020904541, 0.363372981548309
};


float *
get_facemesh_uv_coords (int *num_uvs)
{
    *num_uvs = sizeof (s_face_uv) / sizeof (float) / 2;
    return s_face_uv;
}



/* -------------------------------------------------- *
 * Get face detect result for static face mask
 * -------------------------------------------------- */
int
get_static_facemesh_landmark (face_detect_result_t   *facedet_result,
                              face_landmark_result_t *facemesh_result)
{
    face_t *face = &facedet_result->faces[0];

    memset (face, 0, sizeof (face_t));
    face->score      = 100.0f;
    face->face_cx    = 0.5f;
    face->face_cy    = 0.5f;
    face->face_w     = 1.0f;
    face->face_h     = 1.0f;
    face->face_pos[0].x = 0.0f;
    face->face_pos[0].y = 0.0f;
    face->face_pos[1].x = 1.0f;
    face->face_pos[1].y = 0.0f;
    face->face_pos[2].x = 1.0f;
    face->face_pos[2].y = 1.0f;
    face->face_pos[3].x = 0.0f;
    face->face_pos[3].y = 1.0f;
    facedet_result->num = 1;

    facemesh_result->score = 100.0f;
    for (int i = 0; i < FACE_KEY_NUM; i ++)
    {
        facemesh_result->joint[i].x = s_face_uv[2 * i + 0];
        facemesh_result->joint[i].y = s_face_uv[2 * i + 1];
        facemesh_result->joint[i].z = 0.0f;
    }

    return 0;
}

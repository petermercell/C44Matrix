// C44Matrix.cpp
// Original Author: Ivan Busquets (March 2012)
// Updated for Nuke 16.1 / 17.0 compatibility
//
// A Nuke plugin to apply a 4x4 matrix to pixel data.

static const char* const HELP = "Applies a 4x4 matrix to pixel data.\n"
		"The matrix can be entered manually, or taken"
		" from a camera or axis input.\n";

#include <cstdio>
#include <cmath>
#include <iostream>

#include <DDImage/Convolve.h>
#include "DDImage/PixelIop.h"
#include <DDImage/CameraOp.h>
#include <DDImage/AxisOp.h>
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/ArrayKnobI.h"
#include "DDImage/ValueProvider.h"
#include "DDImage/MetaData.h"
#include "DDImage/DDMath.h"
#include "DDImage/NukeWrapper.h"
#include "DDImage/Matrix3.h"
#include "DDImage/Matrix4.h"


using namespace DD::Image;


// ---------------------------------------------------------------------------
// Helpers: convert between fdk::Mat4d / fdk::Mat4f and DD::Image::Matrix4
//
// worldTransform()    returns const fdk::Mat4d  (double-precision)
// projectionMatrix()  returns const fdk::Mat4d
// ToFormat()          takes  fdk::Mat4f&        (single-precision)
// DD::Image::Matrix4  is its own float-based 4x4 with a flat float[16] array.
// No implicit conversion exists and array() is const, so we build via the
// Matrix4(const float*) constructor.
// ---------------------------------------------------------------------------

static Matrix4 mat4dToMatrix4(const fdk::Mat4d& src)
{
	const double* s = src.array();
	float buf[16];
	for (int i = 0; i < 16; ++i)
		buf[i] = static_cast<float>(s[i]);
	return Matrix4(buf);
}

static Matrix4 mat4fToMatrix4(const fdk::Mat4f& src)
{
	return Matrix4(src.array());
}


// ---------------------------------------------------------------------------
// Helper: extract the desired matrix component from a CameraOp
// ---------------------------------------------------------------------------
static Matrix4 getCameraMatrix(CameraOp* cam, int option, const Format& fmt)
{
	Matrix4 mtx;
	mtx.makeIdentity();

	switch (option) {
	case 0: // full transform
		mtx = mat4dToMatrix4(cam->worldTransform());
		break;
	case 1: // translation only
		mtx = mat4dToMatrix4(cam->worldTransform());
		mtx.translationOnly();
		break;
	case 2: // rotation only
		mtx = mat4dToMatrix4(cam->worldTransform());
		mtx.rotationOnly();
		break;
	case 3: // scale only
		mtx = mat4dToMatrix4(cam->worldTransform());
		mtx.scaleOnly();
		break;
	case 4: // projection
		mtx = mat4dToMatrix4(cam->projectionMatrix());
		break;
	case 5: { // format
		fdk::Mat4f fmtMtx;
		fmtMtx.setToIdentity();
		CameraOp::ToFormat(fmtMtx, &fmt);
		mtx = mat4fToMatrix4(fmtMtx);
		break;
	}
	}
	return mtx;
}

// ---------------------------------------------------------------------------
// Helper: extract the desired matrix component from an AxisOp
// ---------------------------------------------------------------------------
static Matrix4 getAxisMatrix(AxisOp* axis, int option)
{
	Matrix4 mtx;
	mtx.makeIdentity();

	switch (option) {
	case 0: // full transform
		mtx = mat4dToMatrix4(axis->worldTransform());
		break;
	case 1: // translation only
		mtx = mat4dToMatrix4(axis->worldTransform());
		mtx.translationOnly();
		break;
	case 2: // rotation only
		mtx = mat4dToMatrix4(axis->worldTransform());
		mtx.rotationOnly();
		break;
	case 3: // scale only
		mtx = mat4dToMatrix4(axis->worldTransform());
		mtx.scaleOnly();
		break;
	case 4: // projection - not applicable to Axis
		break;
	case 5: // format - not applicable to Axis
		break;
	}
	return mtx;
}


static const char* const matrixFromOptions[] = { "manual input", "from camera/axis input", 0 };
static const char* const cameraMatrixOptions[] = { "transform", "translation", "rotation", "scale", "projection", "format", 0 };

class C44Matrix : public PixelIop, public ValueProvider
{
	int                         _matrixFrom, _matrixOption;
	ChannelSet                  channels;
	Matrix4                     array_mtx;
	ConvolveArray               _arrayKnob;
	bool                        _invert, _transpose, _w_divide;

	// Internal: compute the matrix from the cam/axis input at a given context
	Matrix4 _getInputMatrix(const DD::Image::OutputContext& context) const
	{
		Matrix4 cam_mtx;
		cam_mtx.makeIdentity();

		Op* inputOp = Op::input(1);
		CameraOp* camOp  = dynamic_cast<CameraOp*>(inputOp);
		AxisOp*   axisOp = dynamic_cast<AxisOp*>(inputOp);

		int option = static_cast<int>(
			knob("matrixType")->get_value_at(context.frame(), context.view()));

		if (camOp) {
			camOp->validate();
			cam_mtx = getCameraMatrix(camOp, option, input_format());
		}
		else if (axisOp) {
			axisOp->validate();
			cam_mtx = getAxisMatrix(axisOp, option);
		}
		return cam_mtx;
	}

public:

	C44Matrix(Node* node) : PixelIop(node),
		_matrixFrom(0),
		_matrixOption(0),
		_invert(false),
		_transpose(false),
		_w_divide(false),
		_arrayKnob()
	{}

	bool pass_transform() const override { return true; }
	int minimum_inputs() const override { return 1 + int(_matrixFrom == 1); }
	int maximum_inputs() const override { return 1 + int(_matrixFrom == 1); }
	void knobs(Knob_Callback) override;
	int knob_changed(DD::Image::Knob* k) override;

	static const Iop::Description d;
	const char* Class() const override { return d.name; }
	const char* node_help() const override { return HELP; }

	// -----------------------------------------------------------------------
	// ValueProvider interface
	//
	// In Nuke 16.1 the base class has FOUR pure-virtual methods:
	//   provideValues(const ArrayKnobI*, const OutputContext&) -> vector<double>
	//   provideValuesEnabled(const Knob*, const OutputContext&) -> bool
	//   isDefault(const Knob*, const OutputContext&) -> bool
	//   isAnimated(const Knob*, const OutputContext&) -> bool
	//
	// Plus a non-pure virtual newer overload:
	//   provideValues(double*, size_t, const ArrayKnobI*, const OutputContext&)
	//
	// We implement all five so the class is concrete on 16.1 and forward-
	// compatible with 17.0.
	// -----------------------------------------------------------------------

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

	// Old-style provideValues (returns vector) — pure-virtual in 16.1
	std::vector<double> provideValues(const ArrayKnobI* /*arrayKnob*/,
	                                  const DD::Image::OutputContext& oc) const override
	{
		std::vector<double> values(16);
		Matrix4 cam_mtx;
		cam_mtx.makeIdentity();

		if (knob("matrixFrom")->get_value_at(oc.frame(), oc.view()) == 1)
			cam_mtx = _getInputMatrix(oc);

		const float* mtx_vals = cam_mtx.array();
		for (int i = 0; i < 16; ++i)
			values[i] = static_cast<double>(mtx_vals[i]);

		return values;
	}

#pragma clang diagnostic pop

	// New-style provideValues (writes into buffer) — non-pure in 16.1
	void provideValues(double* values, size_t nValues,
	                   const ArrayKnobI* /*arrayKnob*/,
	                   const DD::Image::OutputContext& oc) const override
	{
		Matrix4 cam_mtx;
		cam_mtx.makeIdentity();

		if (knob("matrixFrom")->get_value_at(oc.frame(), oc.view()) == 1)
			cam_mtx = _getInputMatrix(oc);

		const float* mtx_vals = cam_mtx.array();
		const size_t count = std::min(nValues, size_t(16));
		for (size_t i = 0; i < count; ++i)
			values[i] = static_cast<double>(mtx_vals[i]);
	}

	bool provideValuesEnabled(const DD::Image::Knob* /*knob*/,
	                          const DD::Image::OutputContext& /*oc*/) const override
	{
		return (this->knob("matrixFrom")->get_value() == 1);
	}

	bool isDefault(const DD::Image::Knob* /*knob*/,
	               const DD::Image::OutputContext& /*oc*/) const override
	{
		return false;
	}

	bool isAnimated(const DD::Image::Knob* /*knob*/,
	                const DD::Image::OutputContext& /*oc*/) const override
	{
		return (_matrixFrom == 1);  // animated when driven by cam/axis input
	}

	// -----------------------------------------------------------------------

	void _validate(bool) override;
	void _request(int x, int y, int r, int t, ChannelMask channels, int count) override;

	void in_channels(int input, ChannelSet& mask) const override {
		if (input == 0)
			mask += Mask_RGBA;
	}

	void pixel_engine(const Row& in, int y, int x, int r,
	                  ChannelMask channels, Row& out) override;

	bool test_input(int n, Op* op) const override {
		if (n >= 1)
			return (dynamic_cast<CameraOp*>(op) != 0) ||
			       (dynamic_cast<AxisOp*>(op) != 0);
		return Iop::test_input(n, op);
	}

	Op* default_input(int input) const override {
		if (input == 1)
			return CameraOp::default_camera();
		return Iop::default_input(input);
	}

	const char* input_label(int input, char*) const override {
		switch (input) {
		case 0: return "img";
		case 1: return "cam/axis";
		default: return nullptr;
		}
	}
};


// ---------------------------------------------------------------------------
// Validation / request / engine
// ---------------------------------------------------------------------------

void C44Matrix::_validate(bool for_real)
{
	copy_info();
	array_mtx = Matrix4(_arrayKnob.array);

	if (_transpose)
		array_mtx.transpose();
	if (_invert)
		array_mtx = array_mtx.inverse();

	ChannelSet outchans = channels;
	outchans += Mask_RGBA;
	set_out_channels(outchans);
	info_.turn_on(outchans);
	info_.black_outside(true);
}


void C44Matrix::_request(int x, int y, int r, int t,
                         ChannelMask channels, int count)
{
	ChannelSet requestChans;
	requestChans += channels;
	requestChans += Mask_RGBA;
	input0().request(x, y, r, t, requestChans, count);
}


void C44Matrix::pixel_engine(const Row& in, int y, int x, int r,
                             ChannelMask channels, Row& out)
{
	if (aborted())
		return;

	const float* R = in[Chan_Red];
	const float* G = in[Chan_Green];
	const float* B = in[Chan_Blue];
	const float* A = in[Chan_Alpha];

	float* outX = out.writable(Chan_Red);
	float* outY = out.writable(Chan_Green);
	float* outZ = out.writable(Chan_Blue);
	float* outW = out.writable(Chan_Alpha);

	for (int X = x; X < r; X++) {
		Vector4 sc(R[X], G[X], B[X], A[X]);
		Vector4 pw = array_mtx.transform(sc);

		if (_w_divide)
			pw /= pw.w;

		outX[X] = pw.x;
		outY[X] = pw.y;
		outZ[X] = pw.z;
		outW[X] = pw.w;
	}
}


// ---------------------------------------------------------------------------
// Knobs
// ---------------------------------------------------------------------------

void C44Matrix::knobs(Knob_Callback f)
{
	Enumeration_knob(f, &_matrixFrom, matrixFromOptions, "matrixFrom", "matrix input");
	Tooltip(f, "Choose to enter a 4x4 matrix manually, or take it from a camera or axis input");

	Enumeration_knob(f, &_matrixOption, cameraMatrixOptions, "matrixType", "matrix type");
	Tooltip(f, "Choose the kind of matrix to get from the input camera/axis\n"
			"transform: full transformation matrix (translation + rotation + scale)\n"
			"translation: only apply translations\n"
			"rotation: only apply rotations\n"
			"scale: only apply scale\n"
			"projection: camera projection matrix (camera only)\n"
			"format: camera format matrix (camera only)\n");

	Array_knob(f, &_arrayKnob, 4, 4, "matrix");
	SetValueProvider(f, this);

	Divider(f);

	Bool_knob(f, &_invert, "invert");
	SetFlags(f, Knob::STARTLINE);
	Bool_knob(f, &_transpose, "transpose");
	Bool_knob(f, &_w_divide, "w_divide");
	Tooltip(f, "Divide the resulting vector by its w component.\n"
			"The result will be red/alpha, green/alpha, blue/alpha, 1.0");
}


int C44Matrix::knob_changed(DD::Image::Knob* k)
{
	if (k == &DD::Image::Knob::showPanel) {
		knob("matrixType")->visible(_matrixFrom == 1);
		return 1;
	}

	if (k->is("matrixFrom")) {
		knob("matrixType")->visible(_matrixFrom == 1);
		return 1;
	}

	return PixelIop::knob_changed(k);
}


// ---------------------------------------------------------------------------
// Registration — suppress the Description(name, menu, build) deprecation
// warning since this form works on both 16.1 and 17.0.
// ---------------------------------------------------------------------------

static Iop* build(Node* node) { return new C44Matrix(node); }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
const Iop::Description C44Matrix::d("C44Matrix", "Color/C44Matrix", build);
#pragma clang diagnostic pop
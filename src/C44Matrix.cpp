// C44Matrix.cpp
// Author: Ivan Busquets
// Date: March 2012
// A Nuke plugin to to apply a 4x4 matrix to pixel data

static const char* const HELP = "Applies a 4x4 matrix to pixel data.\n"
		"The matrix can be entered manually, or taken"
		" from a camera or axis input.\n";

#include <stdio.h>
#include <math.h>
#include <iostream>
#include <DDImage/Convolve.h>
#include "DDImage/PixelIop.h"
#include <DDImage/CameraOp.h>
#include <DDImage/AxisOp.h>
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/ArrayKnobI.h"
#include "DDImage/MetaData.h"
#include "DDImage/DDMath.h"
#include "DDImage/NukeWrapper.h"
#include "DDImage/Matrix3.h"
#include "DDImage/Matrix4.h"


using namespace DD::Image;



static const char* const matrixFromOptions[] = { "manual input", "from camera/axis input", 0};
static const char* const cameraMatrixOptions[] = { "transform", "translation", "rotation", "scale", "projection", "format" , 0};
class C44Matrix : public PixelIop, public ArrayKnobI::ValueProvider
{
	int 						_matrixFrom, _matrixOption;
	ChannelSet 					channels;
	Matrix4 					camxforminv, shiftmtx, unproj, array_mtx;
	ConvolveArray		        _arrayKnob;
	bool 						_invert, _transpose, _w_divide;

protected:
	CameraOp* _cam;

public:

	C44Matrix(Node* node) : PixelIop(node),

	_matrixFrom(0),
	_matrixOption(0),
	_invert(false),
	_transpose(false),
	_w_divide(false),
	_arrayKnob()
	{}

	bool pass_transform() const { return true; }
	virtual int minimum_inputs() const { return 1 + int(_matrixFrom == 1); }
	virtual int maximum_inputs() const { return 1 + int(_matrixFrom == 1); }
	virtual void knobs(Knob_Callback);
	int knob_changed(DD::Image::Knob* k);
	static const Iop::Description d;
	const char* Class() const { return d.name; }
	const char* node_help() const { return HELP; }

	// ArrayKnobI stuff
	virtual std::vector<double> provideValues(const ArrayKnobI* arrayKnob, const DD::Image::OutputContext& oc) const;
	bool provideValuesEnabled(const DD::Image::ArrayKnobI* None, const DD::Image::OutputContext& oc) const {
		return (knob("matrixFrom")->get_value()==1);}

	void _validate(bool);
	void _request(int x, int y, int r, int t, ChannelMask channels, int count);
	void in_channels(int input, ChannelSet& mask) const {
		if (input == 0) {
			mask += Mask_RGBA;
		}
	}

	void pixel_engine(const Row &in, int y, int x, int r, ChannelMask channels, Row &out);

	bool test_input(int n, Op *op)  const {   // Test input to accept 1 Iop input and CameraOp or AxisOp inputs

		if (n >= 1) {
			return (dynamic_cast<CameraOp*>(op) != 0) || (dynamic_cast<AxisOp*>(op) != 0);
		}

		return Iop::test_input(n, op);
	}


	Op* default_input(int input) const {
		if (input == 1) {
			return CameraOp::default_camera();
		}

		return Iop::default_input(input);
	}


	const char* input_label(int input, char* buffer) const {
		switch (input) {
		case 0: return "img";
		case 1: return "cam/axis";
		}
	}
};

std::vector<double>
C44Matrix::provideValues(const ArrayKnobI* arrayKnob, const DD::Image::OutputContext& context) const {
	std::vector<double> values;
	Matrix4 cam_mtx;
	cam_mtx.makeIdentity();
	if (knob("matrixFrom")->get_value_at(context.frame(), context.view())==1) {
		Op* inputOp = Op::input(1);
		CameraOp* _camOp = dynamic_cast<CameraOp*>(inputOp);
		AxisOp* _axisOp = dynamic_cast<AxisOp*>(inputOp);
		
		if (_camOp != NULL) {
			_camOp->validate();
			int option = knob("matrixType")->get_value_at(context.frame(), context.view());
			switch (option) {
			case 0:
				cam_mtx = _camOp->matrix();
				break;
			case 1:
				cam_mtx = _camOp->matrix();
				cam_mtx.translationOnly();
				break;
			case 2:
				cam_mtx = _camOp->matrix();
				cam_mtx.rotationOnly();
				break;
			case 3:
				cam_mtx = _camOp->matrix();
				cam_mtx.scaleOnly();
				break;
			case 4:
				cam_mtx = _camOp->projection();
				break;
			case 5:
				_camOp->to_format(cam_mtx, &input_format());
				break;

			}
		}
		else if (_axisOp != NULL) {
			_axisOp->validate();
			int option = knob("matrixType")->get_value_at(context.frame(), context.view());
			switch (option) {
			case 0:
				cam_mtx = _axisOp->matrix();
				break;
			case 1:
				cam_mtx = _axisOp->matrix();
				cam_mtx.translationOnly();
				break;
			case 2:
				cam_mtx = _axisOp->matrix();
				cam_mtx.rotationOnly();
				break;
			case 3:
				cam_mtx = _axisOp->matrix();
				cam_mtx.scaleOnly();
				break;
			case 4:
				// Projection doesn't apply to Axis, use identity
				cam_mtx.makeIdentity();
				break;
			case 5:
				// Format doesn't apply to Axis, use identity
				cam_mtx.makeIdentity();
				break;

			}
		}
	}

	const float* mtx_vals = cam_mtx.array();
	int i = 0;
	while (i<16) {
		values.push_back(mtx_vals[i++]);
	}

	return values;
}


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


void C44Matrix::_request(int x, int y, int r, int t, ChannelMask
		channels, int count)
{

	ChannelSet requestChans;
	requestChans += channels;
	//if (channels.contains(Mask_RGBA)) {
	requestChans += Mask_RGBA;
	//}
	input0().request(x, y, r, t, requestChans, count);
}


void C44Matrix::pixel_engine(const Row &in, int y, int x, int r, ChannelMask channels, Row &out)
{

	if (aborted())
		return;


	const float* R = in[Chan_Red];
	const float* G = in[Chan_Green];
	const float* B = in[Chan_Blue];
	const float* A = in[Chan_Alpha];


	float* outX = out.writable(Chan_Red) ;
	float* outY = out.writable(Chan_Green) ;
	float* outZ = out.writable(Chan_Blue);
	float* outW = out.writable(Chan_Alpha);

	float* END = outX + (r - x);

	for (int X = x; X < r; X++) {
		Vector4 pw;

		Vector4 sc(R[X] , G[X], B[X], A[X]);
		pw =  array_mtx.transform(sc);
		if (_w_divide)
			pw /= pw.w;

		outX[X] = pw.x;
		outY[X] = pw.y;
		outZ[X] = pw.z;
		outW[X] = pw.w;

	}

}





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
			"format: camera format matrix (camera only)\n"
			"");

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
	if(k == &DD::Image::Knob::showPanel) {
		knob("matrixType")->visible(_matrixFrom==1);
		return 1;
	}

	if(k->is("matrixFrom")) {
		knob("matrixType")->visible(_matrixFrom==1);
		return 1;
	}

	return PixelIop::knob_changed(k);
}


static Iop* build(Node* node) { return new C44Matrix(node); }
const Iop::Description
C44Matrix::d("C44Matrix","Color/C44Matrix", build);

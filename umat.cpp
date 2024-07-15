#include <Eigen/Eigen>
#include <iostream>
#include <math.h>
#include <stdlib.h>
#include "func.h"

using namespace std;
void CalcElaStiffMat(Eigen::Ref<Eigen::Matrix<double, 6, 6>> C,
	const double &G,
	const double &LAMBDA)
{
	C = Eigen::Matrix<double, 6, 6>::Zero();
	// Kelvin Voigt
	for (int i = 0; i < 3; i++)
	{
		C(i, i) += 2 * G;
		C(i + 3, i + 3) += G;
		for (int j = 0; j < 3; j++)
		{
			C(i, j) += LAMBDA;

		}
	}
}

extern "C" void umat(double* stress, double* statev, double* ddsdde, double* sse, double* spd,
	double* scd, double* rpl, double* ddsddt, double* drplde, double* drpldt,
	double* stran, double* dstran, double* time, double* dtime, double* temp,
	double* dtemp, double* predef, double* dpred, char* cmname, int* ndi,
	int* nshr, int* ntens, int* nstatv, double* props, int* nprops,
	double* coords, double* drot, double* pnewdt, double* celent, double* dfgrd0,
	double* dfgrd1, int* noel, int* npt, int* layer, int* kspt,
	int* kstep, int* kinc, short cmname_len)
{
	double E = props[0]; //210000Pa  Young's Modulus
	double NU = props[1]; //0.3 Possion's ratio
	double Sy = props[2]; //1000Pa unaxial yield strength
	double xn = props[3]; //0.5 strain hardening exponent
	double G = E / 2.0 / (1.0 + NU); //80769,23
	double LAMBDA = 2.0 * G * NU / (1.0 - 2.0 * NU);//121153,85
	double torer = 1.0e-6;
	int iter = 20; //max number of steps of newton iteration
	double mises;
	double Sf; //updated yield strength from hardening model
	double eqplas = statev[12];
	double deqpl;
	Eigen::MatrixXd dstrain_(6, 1);
	Eigen::MatrixXd stress_(6, 1); //stress in the vector form
	Eigen::MatrixXd eelas(6, 1); //elastic strain in the vector form
	Eigen::MatrixXd eplas(6, 1); //plastic strain in the vector form
	Eigen::MatrixXd flow(6, 1); //plastic strain in the vector form
	Eigen::MatrixXd I1(3, 1); //I1 of stress tensor__vector

	eelas << statev[0], statev[1], statev[2], statev[3], statev[4], statev[5];
	eplas << statev[6], statev[7], statev[8], statev[9], statev[10], statev[11];
	dstrain_ << dstran[0], dstran[1], dstran[2], dstran[3], dstran[4], dstran[5];
	stress_ << stress[0], stress[1], stress[2], stress[3], stress[4], stress[5];

	// elastic case
	// Calc Isotropic elastic stiffness matrix through Eigen 
	Eigen::Matrix<double, 6, 6> C;
	CalcElaStiffMat(C, G, LAMBDA);

	for (int i = 0; i < 6; i++)
	{
		for (int j = 0; j < 6; j++)
		{
			ddsdde[6 * i + j] = 0.0;
		}
	}
	for (int i = 0; i < 3; i++)
	{
		ddsdde[6 * i + 0] = C(0, i);
		ddsdde[6 * i + 1] = C(1, i);
		ddsdde[6 * i + 2] = C(2, i);
		ddsdde[6 * (i + 3) + (i + 3)] = C(i + 3, i + 3);
	}

	//elastic predictor
	stress_ = stress_ + C * dstrain_;

	for (int i = 0; i < 6; i++) {

		stress[i] = stress_(i);
	}

	eelas = eelas + dstrain_;

	//calculate mises stress sqrt(3J2)
	mises = pow((stress_(0) - stress_(1)), 2) + pow((stress_(1) - stress_(2)), 2) + pow((stress_(2) - stress_(0)), 2);
	mises = mises + 6 * (pow(stress_(3), 2) + pow(stress_(4), 2) + pow(stress_(5), 2));
	mises = sqrt(mises / 2);

	//get yield stress from power hardening law
	Sf = Sy * pow((1.0 + E * eqplas / Sy), xn);

	//determine if yielding
	if (mises > Sf * (1 + torer)) {
		Eigen::MatrixXd i(3, 1);
		i << 1.0, 1.0, 1.0;
		I1 = (stress_(0) + stress_(1) + stress_(2)) / 3.0 * i;
		flow(Eigen::seq(0, 2), Eigen::all) = (stress_(Eigen::seq(0, 2), Eigen::all) - I1) / mises;
		flow(Eigen::seq(3, 5), Eigen::all) = stress_(Eigen::seq(3, 5), Eigen::all) / mises;

		deqpl = 0.0;
		double Et = E * xn * pow((1.0 + E * eqplas / Sy), (xn - 1));

		//newton iteration to satisfy the consistency condtion
		for (int n = 1; n < iter; n++)
		{
			double rhs = mises - 3 * G * deqpl - Sf;
			deqpl = deqpl + rhs / ((3 * G) + Et);
			Sf = Sy * pow((1.0 + E * (eqplas + deqpl) / Sy), xn);
			Et = E * xn * pow((1 + E * (eqplas + deqpl) / Sy), (xn - 1));

			if (abs(rhs) < torer * Sy)
			{
				break;
			}
		}

		//updade stress and strains
		stress_(Eigen::seq(0, 2), Eigen::all) = flow(Eigen::seq(0, 2), Eigen::all) * Sf + I1;
		stress_(Eigen::seq(3, 5), Eigen::all) = flow(Eigen::seq(3, 5), Eigen::all) * Sf;
		eplas(Eigen::seq(0, 2), Eigen::all) = eplas(Eigen::seq(0, 2), Eigen::all) + 3.0 / 2.0 * flow(Eigen::seq(0, 2), Eigen::all) * deqpl;
		eelas(Eigen::seq(0, 2), Eigen::all) = eelas(Eigen::seq(0, 2), Eigen::all) - 3.0 / 2.0 * flow(Eigen::seq(0, 2), Eigen::all) * deqpl;
		eplas(Eigen::seq(3, 5), Eigen::all) = eplas(Eigen::seq(3, 5), Eigen::all) + 3.0 * flow(Eigen::seq(3, 5), Eigen::all) * deqpl; //engineering strain in abaqus
		eelas(Eigen::seq(3, 5), Eigen::all) = eelas(Eigen::seq(3, 5), Eigen::all) - 3.0 * flow(Eigen::seq(3, 5), Eigen::all) * deqpl;
		eqplas = eqplas + deqpl;

		//debug program
		//ofstream myfile;
		//myfile.open("equipla.txt");
		//myfile << eqplas << "\n";
		//myfile.close();

		for (int i = 0; i < 6; i++) {

			stress[i] = stress_(i);
		}

		//formulate the jacobian matrix 
		double effg = G * Sf / mises;
		double efflam = (E / (1.0 - 2.0 * NU) - 2.0 * effg) / 3.0;
		double effhrd = 3.0 * G * Et / (3.0 * G + Et) - 3.0 * effg;

		CalcElaStiffMat(C, effg, efflam);
		for (int i = 0; i < 6; i++) {
			for (int j = 0; j < 6; j++) {
				C(i, j) = C(i, j) + effhrd * flow(i) * flow(j);
			}
		}

		for (int i = 0; i < 6; i++)
		{
			for (int j = 0; j < 6; j++)
			{
				ddsdde[6 * i + j] = 0.0;
			}
		}
		for (int i = 0; i < 3; i++)
		{
			ddsdde[6 * i + 0] = C(0, i);
			ddsdde[6 * i + 1] = C(1, i);
			ddsdde[6 * i + 2] = C(2, i);
			ddsdde[6 * (i + 3) + (i + 3)] = C(i + 3, i + 3);
		}
	}
	//update state variable
	for (int i = 0; i < 6; i++) { statev[i] = eelas(i); statev[i + 6] = eplas(i); }
	statev[12] = eqplas;
}

int main(){
    // Initialize the variables
    double stress[6] = {0};
    double stress_backup[6] = {0};
    double statev[13] = {0};
    double statev_backup[13] = {0};
    double ddsdde[36] = {0};
    double sse = 0, spd = 0, scd = 0, rpl = 0, ddsddt = 0, drplde = 0, drpldt = 0;
    double stran[6] = {0}, dstran[6] = {0}, time = 0, dtime = 0, temp = 0, dtemp = 0;
    double predef = 0, dpred = 0;
    char cmname[1] = {'\0'};
    int ndi = 0, nshr = 0, ntens = 0, nstatv = 13, nprops = 0;
    double props[4] = {210000, 0.3, 100, 0.5};
    double coords[3] = {0}, drot[3] = {0}, pnewdt = 0, celent = 0, dfgrd0[3] = {0}, dfgrd1[3] = {0};
    int noel = 0, npt = 0, layer = 0, kspt = 0, kstep = 0, kinc = 0;
    short cmname_len = 0;

    // Define the deformation rate
    double deformation_rate = 0.01;  // This is just an example value

    // Define the time step
    double dt = 0.1;  // This is just an example value
    //
    // Time-stepping loop
    for (int step = 0; step < 500; ++step) {
        // Initialize the strain increment
        dstran[0] = deformation_rate * dt;
        dstran[1] = -deformation_rate * dt * 0.4866;
        dstran[2] = -deformation_rate * dt * 0.4866;
        dstran[3] = 0;
        dstran[4] = 0;
        dstran[5] = 0;

        Eigen::MatrixXd F(5, 1); F = Eigen::MatrixXd::Zero(5, 1);
        Eigen::MatrixXd dF(5, 5); dF = Eigen::MatrixXd::Zero(5, 5);

        for (int n_iter = 0; n_iter < 10; n_iter++){
            // Call the umat function
            for (int i = 0; i < 13; i++){
                statev[i] = statev_backup[i];
            }
            for (int i = 0; i < 6; i++){
                stress[i] = stress_backup[i];
            }
            umat(stress, statev, ddsdde, &sse, &spd, &scd, &rpl, &ddsddt, &drplde, &drpldt,
                 stran, dstran, &time, &dtime, &temp, &dtemp, &predef, &dpred, cmname, &ndi,
                 &nshr, &ntens, &nstatv, props, &nprops, coords, drot, &pnewdt, &celent, dfgrd0,
                 dfgrd1, &noel, &npt, &layer, &kspt, &kstep, &kinc, cmname_len);
            for (int i = 1; i < 6; i++){
                F(i-1) = stress[i];
                for (int j = 1; j < 6; j++){
                    dF(i-1, j-1) = ddsdde[6*i + j];
                }
            }
            if (F.norm() < 0.1) break;
            Eigen::MatrixXd dX(5,1);
            dX = dF.inverse() * F;
            for (int i = 0; i < 5; i++){
                dstran[i+1] -= 1* dX(i);
            }
        }
        for (int i = 0; i < 13; i++){
            statev_backup[i] = statev[i];
        }
        for (int i = 0; i < 6; i++){
            stress_backup[i] = stress[i];
        }
        for (int i = 0; i < 6; i++){
            stran[i] += dstran[i];
        }
        // Print the results for this step
        std::cout << stran[0] << "," << stran[1] << "," << stran[2] << "," << stran[3] << "," << stran[4] << "," << stran[5] << ",";
        std::cout << stress[0] << "," << stress[1] << "," << stress[2] << "," << stress[3] << "," << stress[4] << "," << stress[5] << ",";
        std::cout << statev[0] << "," << statev[1] << "," << statev[2] << "," << statev[3] << "," << statev[4] << "," << statev[5] << std::endl;
    }

    return 0;
}


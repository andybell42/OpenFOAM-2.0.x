/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "blobsSwirlInjector.H"
#include "addToRunTimeSelectionTable.H"
#include "unitConversion.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
    defineTypeNameAndDebug(blobsSwirlInjector, 0);

    addToRunTimeSelectionTable
    (
        injectorModel,
        blobsSwirlInjector,
        dictionary
    );
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::blobsSwirlInjector::blobsSwirlInjector
(
    const dictionary& dict,
    spray& sm
)
:
    injectorModel(dict, sm),
    blobsSwirlInjectorDict_(dict.subDict(typeName + "Coeffs")),

    coneAngle_(blobsSwirlInjectorDict_.lookup("ConeAngle")),
    coneInterval_(blobsSwirlInjectorDict_.lookup("ConeInterval")),

    cD_(blobsSwirlInjectorDict_.lookup("cD")),
    cTau_(blobsSwirlInjectorDict_.lookup("cTau")),
    A_(blobsSwirlInjectorDict_.lookup("A")),

    angle_(0.0),
    u_(0.0),
    x_(0.0),
    h_(0.0)
{

    if (sm.injectors().size() != coneAngle_.size())
    {
        FatalError
            << "blobsSwirlInjector::blobsSwirlInjector"
            << "(const dictionary& dict, spray& sm)\n"
            << "Wrong number of entries in innerAngle"
            << abort(FatalError);
    }

    scalar referencePressure = sm.p().average().value();

    // correct velocityProfile
    forAll(sm.injectors(), i)
    {
        sm.injectors()[i].properties()->correctProfiles
        (
            sm.fuels(),
            referencePressure
        );
    }
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::blobsSwirlInjector::~blobsSwirlInjector()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::scalar Foam::blobsSwirlInjector::d0
(
    const label n,
    const scalar t
) const
{
    const injectorType& it = injectors_[n].properties();

    scalar c = rndGen_.sample01<scalar>();

    angle_ = degToRad(coneAngle_[n]/2.0 + c*coneInterval_[n]);

    scalar injectedMassFlow = it.massFlowRate(t);

    scalar cosAngle = cos(angle_);

    scalar rhoFuel = sm_.fuels().rho(sm_.ambientPressure(), it.T(t), it.X());

    scalar deltaPressure = deltaPressureInj(t,n);

    calculateHX(n, injectedMassFlow, deltaPressure, t);

    scalar kV = kv(n);

    scalar v = kV*sqrt(2.0*deltaPressure/rhoFuel);

    u_ = v*cosAngle;

    return h_;
}


Foam::vector Foam::blobsSwirlInjector::direction
(
    const label n,
    const label hole,
    const scalar time,
    const scalar d
) const
{
    scalar alpha = sin(angle_);
    scalar dcorr = cos(angle_);
    scalar beta = constant::mathematical::twoPi*rndGen_.sample01<scalar>();

    // randomly distributed vector normal to the injection vector
    vector normal = vector::zero;

    if (sm_.twoD())
    {
        scalar reduce = 0.01;
        // correct beta if this is a 2D run
        // map it onto the 'angleOfWedge'

        beta *=
            (1.0 - 2.0*reduce)
           *sm_.angleOfWedge()
           /(constant::mathematical::twoPi);
        beta += reduce*sm_.angleOfWedge();
        normal =
            alpha
           *(
                sm_.axisOfWedge()*cos(beta)
              + sm_.axisOfWedgeNormal()*sin(beta)
            );
    }
    else
    {
        normal =
            alpha
           *(
                injectors_[n].properties()->tan1(hole)*cos(beta)
              + injectors_[n].properties()->tan2(hole)*sin(beta)
            );
    }

    // set the direction of injection by adding the normal vector
    vector dir =
        dcorr*injectors_[n].properties()->direction(hole, time)
      + normal;
    dir /= mag(dir);

    return dir;
}


Foam::scalar Foam::blobsSwirlInjector::velocity
(
    const label i,
    const scalar time
) const
{
    return u_*sqrt(1.0 + pow(tan(angle_),2.0));
}


Foam::scalar Foam::blobsSwirlInjector::averageVelocity(const label i) const
{
    const injectorType& it = sm_.injectors()[i].properties();

    scalar dt = it.teoi() - it.tsoi();


    scalar injectionPressure = averagePressure(i);

    scalar Tav = it.integrateTable(it.T())/dt;
    scalar rhoFuel = sm_.fuels().rho(sm_.ambientPressure(), Tav, it.X());

    scalar kV = kv(i);

    return  kV*sqrt(2.0*(injectionPressure-sm_.ambientPressure())/rhoFuel);
}


Foam::scalar Foam::blobsSwirlInjector::kv(const label inj) const
{
    return cD_[inj]/cos(angle_) * sqrt((1.0 - x_)/(1.0 + x_));
}


void Foam::blobsSwirlInjector::calculateHX
(
    const label inj,
    const scalar massFlow,
    const scalar dPressure,
    const scalar time
) const
{
    const injectorType& it = injectors_[inj].properties();

    scalar Tfuel = it.T(time);
    scalar rhoFuel = sm_.fuels().rho(sm_.ambientPressure(), Tfuel, it.X());
    scalar muFuel = sm_.fuels().mu(sm_.ambientPressure(), Tfuel, it.X());
    scalar injectorDiameter = it.d();

    x_ = 0.0;

    h_ =
        sqrt
        (
            (A_[inj]*cTau_[inj]*muFuel*massFlow*(1.0 + x_))
           /(
                constant::mathematical::pi
               *injectorDiameter
               *rhoFuel
               *dPressure
               *sqr(1.0 - x_)
            )
        );

    label i;

    for (i=0; i<20; i++)
    {
        h_ =
            sqrt
            (
                (A_[inj]*cTau_[inj]*muFuel*massFlow*(1.0 + x_))
               /(
                    constant::mathematical::pi
                   *injectorDiameter
                   *rhoFuel
                   *dPressure
                   *sqr(1.0 - x_)
                )
            );

        x_ = sqr(1.0 - 2.0*h_/injectorDiameter);
    }

    x_ = sqr(1.0 - 2.0*h_/injectorDiameter);
}


Foam::scalar Foam::blobsSwirlInjector::deltaPressureInj
(
    const scalar time,
    const label inj
) const
{
    return
        injectors_[inj].properties()->injectionPressure(time)
      - sm_.ambientPressure();
}


Foam::scalar Foam::blobsSwirlInjector::averagePressure(const label inj) const
{
    const injectorType& it = sm_.injectors()[inj].properties();

    scalar dt = it.teoi() - it.tsoi();
    return it.integrateTable(it.injectionPressureProfile())/dt;
}


// ************************************************************************* //
